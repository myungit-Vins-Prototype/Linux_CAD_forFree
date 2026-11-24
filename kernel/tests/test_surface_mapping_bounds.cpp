#include "fk_bspline.h"
#include "fk_bspline_surface.h"
#include "fk_cancel.h"
#include "fk_surface_mapping.h"
#include "fk_surface_mapping_bounds.h"
#include "fk_test.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <limits>
#include <memory>
#include <vector>

using namespace ForgeCad::Kernel;

namespace {
using Status = detail::SurfaceMappingBoundStatus;
constexpr double budget = 1e-6;

std::vector<double> clamped(int degree, double lo = 0, double hi = 1) {
    std::vector<double> knots(degree + 1, lo);
    knots.insert(knots.end(), degree + 1, hi);
    return knots;
}

BSplineSurface flatSurface(int degree, Interval u = {0, 1}, Interval v = {0, 1}, double gauge = 1) {
    std::vector<Vec3> poles;
    for (int i = 0; i <= degree; ++i) for (int j = 0; j <= degree; ++j)
        poles.emplace_back(u.lo + u.length() * i / degree, v.lo + v.length() * j / degree, 0);
    return BSplineSurface(degree, degree, clamped(degree, u.lo, u.hi), clamped(degree, v.lo, v.hi),
        degree + 1, degree + 1, poles, std::vector<double>(poles.size(), gauge));
}

BSplineSurface bumpSurface(double height) {
    const std::vector<double> knots{0, 0, 0, 0, 46. / 256., 47. / 256.,
        48. / 256., 49. / 256., 50. / 256., 1, 1, 1, 1};
    std::vector<Vec3> poles;
    for (int i = 0; i < 9; ++i) for (int j = 0; j < 9; ++j)
        poles.emplace_back((knots[i + 1] + knots[i + 2] + knots[i + 3]) / 3.,
            (knots[j + 1] + knots[j + 2] + knots[j + 3]) / 3.,
            i == 4 && j == 4 ? height : 0.);
    return BSplineSurface(3, 3, knots, knots, 9, 9, poles);
}

BSplineSurface curvedSurface(int degree) {
    // Bernstein coefficients for z = u*v/64 + u^2/32 - v^2/128.
    // They describe the SAME polynomial for quadratic and cubic surfaces.
    std::vector<Vec3> poles;
    for (int i = 0; i <= degree; ++i) for (int j = 0; j <= degree; ++j) {
        const double u = double(i) / degree, v = double(j) / degree;
        const double u2 = double(i * (i - 1)) / (degree * (degree - 1));
        const double v2 = double(j * (j - 1)) / (degree * (degree - 1));
        poles.emplace_back(u, v, u * v / 64. + u2 / 32. - v2 / 128.);
    }
    return BSplineSurface(degree, degree, clamped(degree), clamped(degree), degree + 1, degree + 1, poles);
}

detail::SurfaceMappingBoundResult bound(const Surface &target, const Surface &image,
        const Surface &source, SurfaceWrapOptions options = {}, double cap = budget,
        Interval u = {0, 1}, Interval v = {0, 1}) {
    return detail::planarMappingResidualBound(source, target, options, 1, 1, image, u, v, cap);
}

void certified(const detail::SurfaceMappingBoundResult &result, double cap = budget) {
    FK_CHECK(result.status == Status::Certified);
    FK_CHECK(std::isfinite(result.upperBound) && result.upperBound >= 0 && result.upperBound <= cap);
}
}

FK_TEST(MappingBoundsFlatNativeDegreesAndConstantWeightGauge) {
    const Plane source{Frame3()};
    const auto image = flatSurface(3, {0, 1}, {0, 1}, 5);
    for (int degree : {2, 3}) {
        const auto target = flatSurface(degree, {0, 1}, {0, 1}, 7);
        certified(bound(target, image, source));
        certified(bound(target, image, source, {}, budget, {.125, .75}, {.25, .875}));
    }
}

FK_TEST(MappingBoundsInteriorPeakFailsAndHarmlessPeakCertifies) {
    const Plane source{Frame3()};
    const auto image = flatSurface(3);
    const auto missed = bumpSurface((9. / 4.) * 1e-4);
    // Independent cardinal-basis oracle: B_4(3/16)^2 = 4/9.
    // The real residual at this interior point is 100 times the cap.
    FK_CHECK_NEAR(missed.point(3. / 16., 3. / 16.).z(), 1e-4, 1e-16);
    FK_CHECK(bound(missed, image, source).status == Status::Failed);
    const auto harmless = bumpSurface(budget / 16.);
    // Positive partition of unity bounds the ENTIRE Z residual by the
    // largest Z pole, budget/16, independently of sampled peak detection.
    // The remaining margin covers IEEE Greville XY rounding.
    certified(bound(harmless, image, source));
    // Also cover narrow spans in the emitted surface, not only the target.
    FK_CHECK(bound(image, missed, source).status == Status::Failed);
    certified(bound(image, harmless, source));
}

FK_TEST(MappingBoundsBilinearAndQuadraticCoefficientOracles) {
    const Plane source{Frame3()};
    const auto image = curvedSurface(3), wrongImage = flatSurface(3);
    constexpr double expectedCenterHeight = (1. / 64. + 1. / 32. - 1. / 128.) / 4.;
    for (int degree : {2, 3}) {
        const auto target = curvedSurface(degree);
        FK_CHECK_NEAR(target.point(.5, .5).z(), expectedCenterHeight, 1e-16);
        FK_CHECK_NEAR(image.point(.5, .5).z(), expectedCenterHeight, 1e-16);
        certified(bound(target, image, source));
        FK_CHECK(bound(target, wrongImage, source).status == Status::Failed);
    }
}

FK_TEST(MappingBoundsDiscreteAxisPermutationAndNegativeUV) {
    // Exact discrete axes, no sin(pi/2): source(u,v)=(-v,u,0).
    const Plane source{Frame3::fromAxes(Vec3(), Vec3(0, 1, 0), Vec3(-1, 0, 0), Vec3(0, 0, 1))};
    const auto target = flatSurface(2, {-1, 0}, {0, 1});
    std::vector<Vec3> poles;
    for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j)
        poles.emplace_back(-j / 3., i / 3., 0);
    const BSplineSurface image(3, 3, clamped(3), clamped(3), 4, 4, poles);
    certified(bound(target, image, source));
    FK_CHECK(distance(image.point(.25, .75), Vec3(-.75, .25, 0)) <= 1e-15);
    const auto wrongImage = flatSurface(3);
    FK_CHECK(bound(target, wrongImage, source).status == Status::Failed);
}

FK_TEST(MappingBoundsInvalidInputsFailAndNonzeroOffsetParticipatesInResidual) {
    const Plane source{Frame3()};
    const auto target = flatSurface(2), image = flatSurface(3);
    const double infinity = std::numeric_limits<double>::infinity();
    const double nan = std::numeric_limits<double>::quiet_NaN();
    for (double cap : {0., -1., infinity, nan})
        FK_CHECK(bound(target, image, source, {}, cap).status == Status::Failed);
    for (Interval query : {Interval{0, infinity}, Interval{nan, 1}, Interval{1, 0}, Interval{-.01, 1}})
        FK_CHECK(bound(target, image, source, {}, budget, query).status == Status::Failed);
    SurfaceWrapOptions options;
    options.shift = {nan, 0};
    FK_CHECK(bound(target, image, source, options).status == Status::Failed);
    options = {};
    options.normalOffset = .001;
    FK_CHECK(bound(target, image, source, options).status == Status::Failed);
    // Variable weights are recognized; this image misses their rational XY distortion.
    std::vector<Vec3> poles;
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j)
        poles.emplace_back(i / 2., j / 2., 0);
    std::vector<double> weights(9, 1.);
    weights[4] = 2.;
    const BSplineSurface rational(2, 2, clamped(2), clamped(2), 3, 3, poles, weights);
    FK_CHECK(bound(rational, image, source).status == Status::Failed);
}

FK_TEST(MappingBoundsPresetCancellationRemainsDistinct) {
    const Plane source{Frame3()};
    const auto target = flatSurface(2), image = flatSurface(3);
    const auto token = std::make_shared<std::atomic<bool>>(true);
    bool cancelled = false;
    {
        CancellationScope scope(token);
        try {
            (void)bound(target, image, source);
        } catch (const OperationCancelled &) {
            cancelled = true;
        }
    }
    FK_CHECK(cancelled);
}

namespace {
BSplineSurface variableWeightPlane(int degree, bool tilted, bool degenerate = false) {
    std::vector<Vec3> poles;
    std::vector<double> weights;
    for (int i = 0; i <= degree; ++i) for (int j = 0; j <= degree; ++j) {
        const double weight = 1. + .5 * i / degree;
        // X numerator is u + .5*u^2; W=1+.5*u, hence X/W=u.
        const double x = (double(i) / degree
            + .5 * i * (i - 1) / (degree * (degree - 1))) / weight;
        const double y = degenerate ? 0. : double(j) / degree;
        poles.emplace_back(x, y, tilted ? .25 * x + .125 * y : 0.);
        weights.push_back(weight);
    }
    return BSplineSurface(degree, degree, clamped(degree), clamped(degree),
        degree + 1, degree + 1, poles, weights);
}

BSplineSurface offsetPlaneImage(bool tilted, double offset) {
    const Vec3 normal = normalized(tilted ? Vec3(-.25, -.125, 1) : Vec3(0, 0, 1));
    std::vector<Vec3> poles;
    for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) {
        const double u = i / 3., v = j / 3.;
        poles.push_back(Vec3(u, v, tilted ? .25 * u + .125 * v : 0.) + offset * normal);
    }
    return BSplineSurface(3, 3, clamped(3), clamped(3), 4, 4, poles);
}
}

FK_TEST(MappingBoundsVariableWeightsCertifyIdentityAndTiltedNormalOffset) {
    const Plane source{Frame3()};
    for (int degree : {2, 3}) for (bool tilted : {false, true}) {
        const auto target = variableWeightPlane(degree, tilted);
        for (double offset : {0., .002}) {
            SurfaceWrapOptions options;
            options.normalOffset = offset;
            const auto image = offsetPlaneImage(tilted, offset);
            // The exact target is (u,v,.25*u+.125*v) or (u,v,0),
            // independently of the nonconstant homogeneous denominator.
            const Vec3 point(.375, .625, tilted ? .25 * .375 + .125 * .625 : 0.);
            FK_CHECK(distance(target.point(.375, .625), point) <= 1e-15);
            certified(bound(target, image, source, options));
            certified(bound(target, image, source, options, budget, {.125, .875}, {.25, .75}));
            if (offset != 0.)
                FK_CHECK(bound(target, offsetPlaneImage(tilted, 0.), source, options).status == Status::Failed);
        }
    }
}

FK_TEST(MappingBoundsVariableWeightDegenerateNormalFailsBeforeCertification) {
    const Plane source{Frame3()};
    const auto image = flatSurface(3);
    SurfaceWrapOptions options;
    options.normalOffset = .001;
    for (int degree : {2, 3}) {
        const auto target = variableWeightPlane(degree, false, true);
        // The second tangent is identically zero over the whole patch.
        FK_CHECK(distance(target.point(.25, 0), target.point(.25, 1)) <= 1e-15);
        FK_CHECK(bound(target, image, source, options).status == Status::Failed);
    }
}

FK_TEST(MappingBoundsTrueRationalCurvatureWithOffsetEnclosesMeasuredResidual) {
    const Plane source{Frame3()};
    constexpr double delta = 1e-7, height = .001, cap = 1e-6;
    std::vector<Vec3> poles;
    std::vector<double> weights;
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) {
        const double weight = 1. + .5 * i / 2.;
        // X=u, Y=v, but Z=delta*u/(1+.5*u) is a true quotient.
        poles.emplace_back((i / 2. + .5 * i * (i - 1) / 2.) / weight,
            j / 2., delta * i / (2. * weight));
        weights.push_back(weight);
    }
    const BSplineSurface target(2, 2, clamped(2), clamped(2), 3, 3, poles, weights);
    std::vector<Vec3> imagePoles;
    for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j)
        imagePoles.emplace_back(i / 3., j / 3., height + delta * i / 3.);
    const BSplineSurface image(3, 3, clamped(3), clamped(3), 4, 4, imagePoles);
    SurfaceWrapOptions options;
    options.normalOffset = height;
    const ExtrusionSurface extrusion(target.vIso(0.), Vec3(0, 1, 0));
    for (const Surface *carrier : std::array<const Surface *, 2>{&target, &extrusion}) {
        const auto result = bound(*carrier, image, source, options, cap);
        certified(result, cap);
        double measuredMaximum = 0.;
        for (int i = 0; i <= 32; ++i) for (int j = 0; j <= 32; ++j) {
            const double u = i / 32., v = j / 32.;
            FK_CHECK_NEAR(carrier->point(u, v).z(), delta * u / (1. + .5 * u), 1e-20);
            const auto expected = carrier->point(u, v) + height * carrier->normal(u, v);
            const double residual = distance(image.point(u, v), expected);
            measuredMaximum = std::max(measuredMaximum, residual);
            // No numerical slack: the continuous enclosure must dominate the
            // independently evaluated residual, including all four boundaries.
            FK_CHECK(residual <= result.upperBound);
        }
        FK_CHECK(measuredMaximum > 3e-8);
        FK_CHECK(bound(*carrier, image, source, options, 1e-9).status == Status::Failed);
    }
}

FK_TEST(MappingBoundsRationalEndSpanDenominatorCrossingWithinSlackFails) {
    const Plane source{Frame3()};
    const auto image = flatSurface(3);
    constexpr double tinyWeight = 1e-14, extrapolation = 5e-13;
    std::vector<Vec3> poles;
    std::vector<double> weights;
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) {
        poles.emplace_back(i / 2., j / 2., 0.);
        weights.push_back(i == 0 ? tinyWeight : 1.);
    }
    const BSplineSurface target(2, 2, clamped(2), clamped(2), 3, 3, poles, weights);
    auto denominator = [](double u) {
        return tinyWeight * (1. - u) * (1. - u) + 2. * u * (1. - u) + u * u;
    };
    FK_CHECK(extrapolation < detail::wrapTargetParameterSlack);
    FK_CHECK(denominator(-extrapolation) < 0. && denominator(extrapolation) > 0.);
    SurfaceWrapOptions options;
    options.anchor = {-extrapolation, 0};
    // The requested UV image lies within the permitted endpoint slack,
    // but its homogeneous denominator crosses zero inside that image.
    FK_CHECK(bound(target, image, source, options, budget, {0, 2 * extrapolation}).status == Status::Failed);
    const ExtrusionSurface extrusion(target.vIso(0.), Vec3(0, 1, 0));
    FK_CHECK(bound(extrusion, image, source, options, budget, {0, 2 * extrapolation}).status == Status::Failed);
}

namespace {
BSplineSurface periodicBoundCarrier(bool alongU, double height) {
    const std::vector<double> unit{0, 0, 0, 0, 46. / 256., 47. / 256.,
        48. / 256., 49. / 256., 50. / 256., 1, 1, 1, 1};
    auto periodicKnots = unit;
    for (double &k : periodicKnots) k += 2.;
    std::vector<Vec3> poles;
    const int nu = alongU ? 9 : 4, nv = alongU ? 4 : 9;
    for (int i = 0; i < nu; ++i) for (int j = 0; j < nv; ++j) {
        const int periodicIndex = alongU ? i : j;
        poles.emplace_back(alongU ? 0. : i / 3., alongU ? j / 3. : 0.,
            periodicIndex == 4 ? height : 0.);
    }
    return BSplineSurface(3, 3, alongU ? periodicKnots : clamped(3),
        alongU ? clamped(3) : periodicKnots, nu, nv, poles, {}, alongU, !alongU);
}
}

FK_TEST(MappingBoundsPeriodicTargetSeamNonzeroOriginAndMultipleTurns) {
    const Plane source{Frame3()};
    for (bool alongU : {true, false}) {
        const auto flat = periodicBoundCarrier(alongU, 0.);
        const auto harmless = periodicBoundCarrier(alongU, budget / 16.);
        const auto missed = periodicBoundCarrier(alongU, 1.5e-4);
        for (bool negative : {false, true}) for (double anchor : {2., 2.875, 5.875}) for (double turns : {1., 4.}) {
            SurfaceWrapOptions options;
            options.sourcePlane = Frame3::fromAxes(Vec3(),
                Vec3(negative ? -1 : 1, 0, 0), Vec3(0, negative ? -1 : 1, 0), Vec3(0, 0, 1));
            options.anchor = alongU ? Vec2(anchor, negative ? 1. : 0.)
                                    : Vec2(negative ? 1. : 0., anchor);
            // The image follows the nonperiodic coordinate, including reversed UV.
            std::vector<Vec3> actualPoles;
            for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) {
                const double q = (alongU ? j : i) / 3.;
                actualPoles.emplace_back(alongU ? 0. : (negative ? 1. - q : q),
                    alongU ? (negative ? 1. - q : q) : 0., 0.);
            }
            const BSplineSurface actual(3, 3, clamped(3), clamped(3), 4, 4, actualPoles);
            const auto periodicBound = [&](const Surface &target) {
                return detail::planarMappingResidualBound(source, target, options,
                    alongU ? 1. / turns : 1., alongU ? 1. : 1. / turns,
                    actual, {0, 1}, {0, 1}, budget);
            };
            certified(periodicBound(flat));
            certified(periodicBound(harmless));
            // Cardinal cubic maximum is 2/3, so its periodic image has
            // a 1e-4 residual in every complete turn, independently of samples.
            const double witness = 2. + 3. / 16. + (anchor >= 5. ? 3. : 0.);
            FK_CHECK_NEAR((alongU ? missed.point(witness, .5) : missed.point(.5, witness)).z(), 1e-4, 1e-15);
            FK_CHECK(periodicBound(missed).status == Status::Failed);
        }
    }
}

FK_TEST(MappingBoundsPeriodicCurvedRegularSeamCertifiesAndDiscontinuousSeamFails) {
    const Plane source{Frame3()};
    const std::vector<double> knots{0, 0, 0, 0, .25, .25, .25, .75, .75, .75, 1, 1, 1, 1};
    // End spans share the regular C1 parabola (t,v,t*t) across the seam:
    // t=U in the first span, t=U-1 in the last span.
    const std::vector<double> xs{0, 1. / 12., 1. / 6., .25, .25, -.25, -.25, -1. / 6., -1. / 12., 0};
    const std::vector<double> zs{0, 0, 1. / 48., 1. / 16., 1. / 16., 1. / 16., 1. / 16., 1. / 48., 0, 0};
    for (bool alongU : {true, false}) {
        std::vector<Vec3> imagePoles;
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j) {
                const int k = alongU ? i : j;
                const double t = -.125 + .25 * k / 3.;
                // Cubic Bernstein coefficients of (-1/8+u/4)^2.
                const double z = 1. / 64. - k / 48. + k * (k - 1) / 96.;
                imagePoles.emplace_back(t, (alongU ? j : i) / 3., z);
            }
        const BSplineSurface image(3, 3, clamped(3), clamped(3), 4, 4, imagePoles);
        SurfaceWrapOptions options;
        options.anchor = alongU ? Vec2(.875, 0) : Vec2(0, .875);
        for (bool discontinuous : {false, true}) {
            std::vector<Vec3> poles;
            for (int i = 0; i < (alongU ? 10 : 4); ++i)
                for (int j = 0; j < (alongU ? 4 : 10); ++j) {
                    const int k = alongU ? i : j;
                    poles.emplace_back(xs[k], (alongU ? j : i) / 3.,
                                       zs[k] + (discontinuous && k == 9 ? .001 : 0.));
                }
            const BSplineSurface target(3, 3, alongU ? knots : clamped(3), alongU ? clamped(3) : knots,
                                        alongU ? 10 : 4, alongU ? 4 : 10, poles, {}, alongU, !alongU);
            const auto result = detail::planarMappingResidualBound(
                source, target, options, alongU ? 4. : 1., alongU ? 1. : 4., image, {0, 1}, {0, 1}, budget);
            if (discontinuous)
                FK_CHECK(result.status == Status::Failed);
            else {
                certified(result);
                for (double u : {0., .25, .5, .75, 1.}) {
                    const double t = .25 * u - .125;
                    const Vec3 wanted(t, .375, t * t);
                    FK_CHECK(distance(alongU ? target.point(.875 + .25 * u, .375)
                                             : target.point(.375, .875 + .25 * u),
                                      wanted) <= 1e-15);
                    FK_CHECK(distance(alongU ? image.point(u, .375) : image.point(.375, u), wanted) <= 1e-15);
                }
                const auto n = alongU ? target.normal(1., .375) : target.normal(.375, 1.);
                FK_CHECK(distance(n, Vec3()) > .99);
            }
        }
    }
}

FK_TEST(MappingBoundsPeriodicTurnLimitsFailTerminally) {
    const Plane source{Frame3()};
    const auto target = periodicBoundCarrier(true, 0.);
    std::vector<Vec3> poles;
    for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j)
        poles.emplace_back(0., j / 3., 0.);
    const BSplineSurface image(3, 3, clamped(3), clamped(3), 4, 4, poles);
    SurfaceWrapOptions options;
    options.anchor = {2., 0};
    certified(bound(target, image, source, options, budget));
    for (double anchor : {-0x1p53, 0x1p53}) {
        options.anchor = {anchor, 0};
        FK_CHECK(bound(target, image, source, options, budget).status == Status::Failed);
    }
    options.anchor = {2., 0};
    FK_CHECK(detail::planarMappingResidualBound(source, target, options, 1. / 512., 1.,
        image, {0, 1}, {0, 1}, budget).status == Status::Failed);
}

FK_TEST(MappingBoundsNearQuarterTurnRetainsTransverseTravel) {
    const Plane source{Frame3()};
    const auto target = flatSurface(3, {0, 2}, {0, 2});
    constexpr double length = 0x1p48, anchor = 1.125;
    std::vector<Vec3> poles;
    for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j)
        poles.emplace_back(anchor - j / 3., i / 3., 0.);
    const BSplineSurface image(3, 3, clamped(3, 0, length), clamped(3), 4, 4, poles);
    SurfaceWrapOptions options;
    options.anchor = {anchor, 0};
    options.angle = kHalfPi;
    // Dropping cos(angle) would make the flat image look exact. Over this
    // travel the lost U component is more than one hundredth of a mm.
    FK_CHECK(std::abs(std::cos(options.angle) * length) > .01);
    const auto result = detail::planarMappingResidualBound(source, target, options, 1., length,
        image, {0, length}, {0, 1}, budget);
    FK_CHECK(result.status == Status::Failed);
}

FK_TEST(MappingBoundsOpenBSplineExtrusionUsesNaturalOffsetAndUnboundedV) {
    const Plane source{Frame3()};
    const Vec3 axis(2., 3., 6.), direction = axis / 7.;
    const Vec3 tangent(3., -2., 0.), normal = normalized(cross(tangent, direction));
    // C(u) = u*tangent + (u*u-u)*direction. For the rational
    // representation, multiply both terms by W(u) = 1 + u/2 before
    // taking the cubic Bernstein coefficients and dividing by each weight.
    std::vector<Vec3> curvePoles, rationalPoles;
    std::vector<double> weights;
    for (int i = 0; i <= 3; ++i) {
        const double u1 = i / 3., u2 = i * (i - 1) / 6., u3 = i == 3 ? 1. : 0.;
        const double weight = 1. + .5 * u1;
        curvePoles.push_back(u1 * tangent + (u2 - u1) * direction);
        rationalPoles.push_back(((u1 + .5 * u2) * tangent
            + (.5 * u3 + .5 * u2 - u1) * direction) / weight);
        weights.push_back(weight);
    }
    const auto polynomial = BSplineCurve<3>(3, clamped(3), curvePoles).insertKnot(.375);
    const auto rational = BSplineCurve<3>(3, clamped(3), rationalPoles, weights).insertKnot(.375);
    constexpr double height = .002;
    SurfaceWrapOptions options;
    options.anchor = {0., -3.};
    options.normalOffset = height;
    auto offsetImage = [&](double offset) {
        std::vector<Vec3> poles;
        for (const auto &pole : polynomial.poles()) for (double v : {-3., -1., 1., 3.})
            poles.push_back(pole + v * direction + offset * normal);
        return BSplineSurface(3, 3, polynomial.knots(), clamped(3, 0, 6),
            polynomial.poleCount(), 4, poles);
    };
    const auto image = offsetImage(height), opposite = offsetImage(-height);
    for (const auto *generatrix : {&polynomial, &rational}) {
        const ExtrusionSurface target(std::make_shared<BSplineCurve<3>>(*generatrix), axis);
        FK_CHECK(!target.isUPeriodic());
        const auto result = bound(target, image, source, options, budget, {0., 1.}, {0., 6.});
        certified(result);
        for (double u : {0., .375, 1.}) for (double v : {0., 3., 6.}) {
            FK_CHECK(distance(target.normal(u, v - 3.), normal) <= 1e-14);
            const auto expected = u * tangent + (u * u - u + v - 3.) * direction + height * normal;
            FK_CHECK(distance(target.point(u, v - 3.) + height * normal, expected) <= 1e-14);
            FK_CHECK(distance(image.point(u, v), expected) <= 1e-14);
            FK_CHECK_NEAR(distance(opposite.point(u, v), expected), 2. * height, 1e-14);
        }
        FK_CHECK(bound(target, opposite, source, options, budget, {0., 1.}, {0., 6.}).status == Status::Failed);
    }
}

FK_TEST(MappingBoundsTranslatedRationalExtrusionEnclosesOneUlpResidual) {
    const Plane source{Frame3()};
    constexpr double translation = 0x1p40, step = 0x1p-12;
    // The denominators are W(u) = 3 + 3u for the curve and 1 for the
    // image. Both describe Y(u) = 15u: 15u W has Bernstein coefficients
    // {0, 15, 45, 90}. All stored Euclidean poles are exact.
    const std::vector<Vec3> curvePoles{Vec3(translation, 0., 0.),
        Vec3(translation, 3.75, 0.), Vec3(translation, 9., 0.), Vec3(translation, 15., 0.)};
    const auto curve = std::make_shared<BSplineCurve<3>>(3, clamped(3), curvePoles,
        std::vector<double>{3., 4., 5., 6.});
    const ExtrusionSurface target(curve, Vec3(0., 0., 1.));
    std::vector<Vec3> imagePoles;
    for (double y : {0., 5., 10., 15.}) for (double v : {0., 1., 2., 3.})
        imagePoles.emplace_back(translation + step, y, v);
    const BSplineSurface image(3, 3, clamped(3), clamped(3, 0., 3.),
        curve->poleCount(), 4, imagePoles);
    // Each carrier has constant X. Their exact residual is one binary64
    // ULP even when ordinary interior evaluations round independently.
    FK_CHECK(distance(image.point(0., 0.), target.point(0., 0.)) == step);
    const auto result = bound(target, image, source, {}, 2. * step, {0., 1.}, {0., 3.});
    certified(result, 2. * step);
    FK_CHECK(result.upperBound >= step);
    FK_CHECK(bound(target, image, source, {}, .5 * step, {0., 1.}, {0., 3.}).status == Status::Failed);
}

FK_TEST(MappingBoundsExtrusionPreservesNestedTrimDomain) {
    const auto line = std::make_shared<BSplineCurve<3>>(1, clamped(1),
        std::vector<Vec3>{{0., 0., 0.}, {1., 0., 0.}});
    const auto inner = std::make_shared<TrimmedCurve<3>>(line, .125, .875);
    const ExtrusionSurface target(std::make_shared<TrimmedCurve<3>>(inner, .25, .75), Vec3(0, 1, 0));
    const auto image = flatSurface(3);
    const Plane source{Frame3()};
    certified(bound(target, image, source, {}, budget, {.25, .75}));
    FK_CHECK(bound(target, image, source, {}, budget, {.125, .75}).status == Status::Failed);
}

namespace {
CurvePtr<3> closedExtrusionProfile(bool trimmed, double endpointGap = 0., double afterTrim = 1.) {
    if (trimmed) {
        // The closed trim has period 1, while its open B-spline basis spans 5.
        // Before its low endpoint the tangent points in the opposite direction.
        const auto basis = std::make_shared<BSplineCurve<3>>(1,
            std::vector<double>{0., 0., 2., 2.25, 2.75, 3., 5., 5.},
            std::vector<Vec3>{{1., 0., 0.}, {0., 0., 0.}, {.25, 0., 0.},
                {-.25, 0., 0.}, {endpointGap, 0., 0.}, {afterTrim, 0., 0.}});
        return std::make_shared<TrimmedCurve<3>>(basis, 2., 3.);
    }
    return std::make_shared<BSplineCurve<3>>(1,
        std::vector<double>{2., 2., 2.25, 2.75, 3., 3.},
        std::vector<Vec3>{{0., 0., 0.}, {.25, 0., 0.},
            {-.25, 0., 0.}, {endpointGap, 0., 0.}});
}

BSplineSurface closedExtrusionSeamImage(double height) {
    std::vector<Vec3> poles;
    for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j)
        poles.emplace_back(-.125 + .25 * i / 3., j / 3., height);
    return BSplineSurface(3, 3, clamped(3), clamped(3), 4, 4, poles);
}
}

FK_TEST(MappingBoundsClosedNonperiodicExtrusionWrapsSeamAndOwnTrimPeriod) {
    const Plane source{Frame3()};
    constexpr double height = .002;
    for (bool trimmed : {false, true}) {
        const auto profile = closedExtrusionProfile(trimmed);
        FK_CHECK(!profile->isPeriodic());
        const ExtrusionSurface target(profile, Vec3(0., 1., 0.));
        FK_CHECK(target.isUPeriodic());
        FK_CHECK_NEAR(target.uPeriod(), 1., 0.);
        for (double anchor : {2.875, -1.125, 6.875}) for (double offset : {0., height}) {
            SurfaceWrapOptions options;
            options.anchor = {anchor, 0.};
            options.normalOffset = offset;
            const auto image = closedExtrusionSeamImage(offset);
            const auto result = detail::planarMappingResidualBound(source, target, options,
                4., 1., image, {0., 1.}, {0., 1.}, budget);
            certified(result);
            for (double u : {0., .25, .5, .75, 1.}) {
                const Vec3 wanted(-.125 + .25 * u, .375, offset);
                FK_CHECK(distance(target.point(anchor + .25 * u, .375)
                    + offset * target.normal(anchor + .25 * u, .375), wanted) <= 1e-14);
                FK_CHECK(distance(image.point(u, .375), wanted) <= 1e-14);
            }
            FK_CHECK(detail::planarMappingResidualBound(source, target, options, 4., 1.,
                closedExtrusionSeamImage(offset + .001), {0., 1.}, {0., 1.}, budget).status == Status::Failed);
        }
    }
}

FK_TEST(MappingBoundsClosedExtrusionFullTurnsRetainHiddenInteriorPeak) {
    const Plane source{Frame3()};
    std::vector<Vec3> poles;
    for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j)
        poles.emplace_back(0., j / 3., 0.);
    const BSplineSurface image(3, 3, clamped(3), clamped(3), 4, 4, poles);
    for (double height : {0., budget / 16., 1.5e-4}) {
        const auto carrier = periodicBoundCarrier(true, height);
        const auto profile = carrier.vIso(0.);
        FK_CHECK(!profile->isPeriodic());
        const ExtrusionSurface target(profile, Vec3(0., 1., 0.));
        FK_CHECK(target.isUPeriodic());
        for (double anchor : {-1.125, 2.875, 6.875}) for (double turns : {1., 4.}) {
            SurfaceWrapOptions options;
            options.anchor = {anchor, 0.};
            const auto result = detail::planarMappingResidualBound(source, target, options,
                1. / turns, 1., image, {0., 1.}, {0., 1.}, budget);
            if (height > budget) {
                FK_CHECK_NEAR(target.point(2. + 3. / 16., .5).z(), 1e-4, 1e-15);
                FK_CHECK(result.status == Status::Failed);
            } else certified(result);
        }
    }
}

FK_TEST(MappingBoundsExtrusionEndpointGapBeyondResolutionDoesNotWrap) {
    const Plane source{Frame3()};
    for (bool trimmed : {false, true}) {
        const auto profile = closedExtrusionProfile(trimmed, 2. * linearResolution());
        const ExtrusionSurface target(profile, Vec3(0., 1., 0.));
        FK_CHECK(!target.isUPeriodic());
        SurfaceWrapOptions options;
        options.anchor = {2.875, 0.};
        FK_CHECK(detail::planarMappingResidualBound(source, target, options, 4., 1.,
            closedExtrusionSeamImage(0.), {0., 1.}, {0., 1.}, budget).status == Status::Failed);
    }
}

FK_TEST(MappingBoundsClosedExtrusionRetainsExactTrimEndpointNormal) {
    // At the exposed high endpoint evaluate() uses the following knot span.
    // Its tangent is reversed at this one point of the native chart.
    const ExtrusionSurface target(closedExtrusionProfile(true, 0., -1.), Vec3(0., 1., 0.));
    const Plane source{Frame3()};
    SurfaceWrapOptions options;
    options.anchor = {2.875, 0.};
    options.normalOffset = .002;
    FK_CHECK(target.normal(3., .5).z() == -1.);
    FK_CHECK(target.normal(3. - 1e-10, .5).z() == 1.);
    FK_CHECK(target.normal(3. + 1e-10, .5).z() == 1.);
    FK_CHECK(detail::planarMappingResidualBound(source, target, options, 4., 1.,
        closedExtrusionSeamImage(.002), {0., 1.}, {0., 1.}, budget).status == Status::Failed);
}

FK_TEST(MappingBoundsClosedExtrusionRejectsUnresolvablePeriodIndices) {
    const ExtrusionSurface target(closedExtrusionProfile(false), Vec3(0., 1., 0.));
    const Plane source{Frame3()};
    for (double anchor : {-0x1p53, 0x1p53}) {
        SurfaceWrapOptions options;
        options.anchor = {anchor, 0.};
        FK_CHECK(detail::planarMappingResidualBound(source, target, options, 4., 1.,
            closedExtrusionSeamImage(0.), {0., 1.}, {0., 1.}, budget).status == Status::Failed);
    }
}

FK_TEST(MappingBoundsClosedExtrusionTrimInBasisEndpointAllowance) {
    const auto basis = std::make_shared<BSplineCurve<3>>(1, clamped(1),
        std::vector<Vec3>{{-1., 0., 0.}, {0., 0., 0.}});
    constexpr double period = 0x1p-42;
    const auto profile = std::make_shared<TrimmedCurve<3>>(basis, 1. + period, 1. + 2. * period);
    const ExtrusionSurface target(profile, Vec3(0., 1., 0.));
    FK_CHECK(target.isUPeriodic() && target.uPeriod() == period);
    SurfaceWrapOptions options;
    options.anchor = {1. + 5. * period, 0.};
    options.normalOffset = .002;
    std::vector<Vec3> poles;
    for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j)
        poles.emplace_back(0., j / 3., options.normalOffset);
    const BSplineSurface image(3, 3, clamped(3), clamped(3), 4, 4, poles);
    const Plane source{Frame3()};
    const auto result = detail::planarMappingResidualBound(source, target, options,
        1. / period, 1., image, {0., 1.}, {0., 1.}, budget);
    certified(result);
    FK_CHECK(result.upperBound >= period);
}

FK_TEST(MappingBoundsTransformedExtrusionNestedTrimsRetainNaturalNormalAndWeights) {
    const Plane source{Frame3()};
    for (double sign : {-1., 1.}) for (bool rational : {false, true}) {
        std::vector<Vec3> curvePoles;
        std::vector<double> weights;
        for (int i = 0; i < 4; ++i) {
            const double u = i / 3., u2 = i * (i - 1) / 6., u3 = i == 3 ? 1. : 0.;
            const double weight = rational ? 1. + .5 * u : 1.;
            curvePoles.emplace_back(rational ? (u + .5 * u2) / weight : u,
                rational ? (.5 * u3 + .5 * u2 - u) / weight : u2 - u, 0.);
            weights.push_back(weight);
        }
        const auto basis = std::make_shared<BSplineCurve<3>>(3, clamped(3), curvePoles, weights);
        const auto innerTrim = std::make_shared<TrimmedCurve<3>>(basis, .125, .875);
        const auto shifted = std::make_shared<TransformedCurve>(innerTrim,
            Transform3::translation(Vec3(1., 2., 3.)));
        const auto outerTrim = std::make_shared<TrimmedCurve<3>>(shifted, .25, .75);
        // sign=-1 is an exact quarter turn around Y; sign=+1 additionally
        // reflects X. Keep the extrusion direction in world coordinates.
        const double matrix[3][3]{{0., 0., 1.}, {0., 1., 0.}, {sign, 0., 0.}};
        const auto rotated = std::make_shared<TransformedCurve>(outerTrim,
            Transform3::fromParts(matrix, Vec3(4., -5., 6.)));
        const ExtrusionSurface target(std::make_shared<TransformedCurve>(rotated,
            Transform3::translation(Vec3(-1., 0., 2.))), Vec3(0., 1., 0.));
        const Vec3 normal(-sign, 0., 0.);
        auto imageAtHeight = [&](double height) {
            std::vector<Vec3> poles;
            for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) {
                const double u = i / 3., u2 = i * (i - 1) / 6.;
                poles.emplace_back(6. - sign * height, -3. + u2 - u - 2. + 5. * j / 3.,
                    8. + sign + sign * u);
            }
            return BSplineSurface(3, 3, clamped(3), clamped(3, -2., 3.), 4, 4, poles);
        };
        for (double height : {0., .002}) {
            SurfaceWrapOptions options;
            options.normalOffset = height;
            const auto image = imageAtHeight(height);
            certified(bound(target, image, source, options, budget, {.25, .75}, {-2., 3.}));
            for (double u : {.25, .5, .75}) for (double v : {-2., .5, 3.}) {
                // Independent polynomial oracle: no transform evaluation or
                // transformed control poles enter the expected position/normal.
                const Vec3 expected(6., -3. + u * u - u + v, 8. + sign + sign * u);
                FK_CHECK(distance(target.point(u, v), expected) <= 1e-14);
                FK_CHECK(distance(target.normal(u, v), normal) <= 1e-14);
                FK_CHECK(distance(image.point(u, v), expected + height * normal) <= 1e-14);
            }
            const auto wrong = imageAtHeight(height == 0. ? .001 : -height);
            FK_CHECK(bound(target, wrong, source, options, budget, {.25, .75}, {-2., 3.}).status == Status::Failed);
        }
        FK_CHECK(bound(target, imageAtHeight(0.), source, {}, budget, {.125, .75}, {-2., 3.}).status == Status::Failed);
    }
}

FK_TEST(MappingBoundsTransformedExtrusionUsesWorldDirectionAfterShear) {
    // This singular placement maps the profile tangent to (3,1,4).
    // It also shears Y, but the extrusion direction is supplied in world coordinates.
    const double shear[3][3]{{3., 1., 0.}, {1., 0., 0.}, {4., -2., 0.}};
    // H(u)=u+u² and W(u)=1+u, so the rational profile is the line X=u.
    const auto line = std::make_shared<BSplineCurve<3>>(2, clamped(2),
        std::vector<Vec3>{{0., 0., 0.}, {1./3., 0., 0.}, {1., 0., 0.}},
        std::vector<double>{1., 1.5, 2.});
    const auto sheared = std::make_shared<TransformedCurve>(line, Transform3::fromParts(shear, {}));
    const auto profile = std::make_shared<TransformedCurve>(sheared, Transform3::translation({0., 1., 0.}));
    const ExtrusionSurface target(profile, Vec3(0., 1., 0.));
    const Plane source{Frame3()};
    const Vec3 normal(-.8, 0., .6);
    SurfaceWrapOptions options;
    options.normalOffset = .002;
    std::vector<Vec3> poles;
    for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j)
        poles.push_back((i / 3.) * Vec3(3., 1., 4.) + Vec3(0., 1. + j / 3., 0.) + options.normalOffset * normal);
    const BSplineSurface image(3, 3, clamped(3), clamped(3), 4, 4, poles);
    FK_CHECK(distance(target.normal(.5, .5), normal) < 1e-14);
    certified(bound(target, image, source, options));
    options.normalOffset *= -1.;
    FK_CHECK(bound(target, image, source, options).status == Status::Failed);
    // With the extrusion along the mapped tangent there is no offset normal.
    const ExtrusionSurface collapsed(profile, Vec3(3., 1., 4.));
    FK_CHECK(bound(collapsed, image, source, options).status == Status::Failed);
}

FK_TEST(MappingBoundsTransformedExtrusionAccountsForIntermediateRounding) {
    const auto line = std::make_shared<BSplineCurve<3>>(1, clamped(1),
        std::vector<Vec3>{{0., 0., 0.}, {1., 0., 0.}});
    constexpr double far = 0x1p52;
    const auto shifted = std::make_shared<TransformedCurve>(line, Transform3::translation({far, 0., 0.}));
    const ExtrusionSurface target(std::make_shared<TransformedCurve>(shifted,
        Transform3::translation({-far, 0., 0.})), Vec3(0., 1., 0.));
    const auto image = flatSurface(3);
    const Plane source{Frame3()};
    // The inner evaluator rounds far + 1/4 to far before the outer translation.
    FK_CHECK(target.point(.25, .5).x() == 0.);
    FK_CHECK_NEAR(image.point(.25, .5).x(), .25, 1e-15);
    FK_CHECK(bound(target, image, source).status == Status::Failed);
}

FK_TEST(MappingBoundsTransformedExtrusionHonorsPlacementBudget) {
    CurvePtr<3> curve = std::make_shared<BSplineCurve<3>>(1, clamped(1),
        std::vector<Vec3>{{0., 0., 0.}, {1., 0., 0.}});
    for (int i = 0; i < 32; ++i)
        curve = std::make_shared<TransformedCurve>(curve, Transform3());
    const Plane source{Frame3()};
    const auto image = flatSurface(3);
    certified(bound(ExtrusionSurface(curve, Vec3(0., 1., 0.)), image, source));
    curve = std::make_shared<TransformedCurve>(curve, Transform3());
    FK_CHECK(bound(ExtrusionSurface(curve, Vec3(0., 1., 0.)), image, source).status == Status::Failed);
}

FK_TEST(MappingBoundsTransformedExtrusionUsesPlacedEndpointGapAtSeam) {
    const Plane source{Frame3()};
    const Vec3 direction(0., 1., 0.);
    SurfaceWrapOptions options;
    options.anchor = {2.875, 0.};
    options.normalOffset = .002;
    for (bool closes : {true, false}) {
        const double gap = linearResolution() * (closes ? 2. : .5);
        const double scale = closes ? -.125 : -8.;
        const auto trim = closedExtrusionProfile(true, gap);
        const ExtrusionSurface native(trim, direction);
        const ExtrusionSurface target(std::make_shared<TransformedCurve>(trim,
            Transform3::scaling({}, scale)), direction);
        FK_CHECK(native.isUPeriodic() != closes);
        FK_CHECK(target.isUPeriodic() == closes);
        std::vector<Vec3> poles;
        for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j)
            poles.emplace_back(scale * (-.125 + .25 * i / 3.), j / 3., -options.normalOffset);
        const BSplineSurface image(3, 3, clamped(3), clamped(3), 4, 4, poles);
        const auto result = detail::planarMappingResidualBound(source, target, options, 4., 1.,
            image, {0., 1.}, {0., 1.}, budget);
        if (closes) {
            certified(result);
            FK_CHECK_NEAR(target.point(3., .5).x(), scale * gap, 1e-15);
            FK_CHECK(result.upperBound >= std::abs(scale * gap));
        } else FK_CHECK(result.status == Status::Failed);
    }
}

namespace {
BSplineSurface angularConicTaylorImage(double a, double b, double height, double displacement = 0.) {
    constexpr double halfWidth = 0x1p-10;
    // Exact cubic Bernstein coefficients of t, t^2 and t^3 on [-d,d].
    const std::array<double, 4> linear{-1., -1. / 3., 1. / 3., 1.};
    const std::array<double, 4> square{1., -1. / 3., -1. / 3., 1.};
    const std::array<double, 4> cube{-1., 1., -1., 1.};
    const double ratio = a / b;
    std::vector<Vec3> poles;
    for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) {
        const double t = halfWidth * linear[i];
        const double t2 = halfWidth * halfWidth * square[i];
        const double t3 = halfWidth * halfWidth * halfWidth * cube[i];
        // C=(a*cos(t),b*sin(t)), N=(b*cos(t),a*sin(t))/|...|.
        // Expand C+h*N independently, without calling a curve evaluator.
        poles.emplace_back(a + height - .5 * (a + height * ratio * ratio) * t2 + displacement,
            (b + height * ratio) * t
                + (-b / 6. + height * ratio * (1. / 3. - .5 * ratio * ratio)) * t3,
            -2. + 5. * j / 3.);
    }
    return BSplineSurface(3, 3, clamped(3, -halfWidth, halfWidth), clamped(3, -2., 3.), 4, 4, poles);
}
}

FK_TEST(MappingBoundsNativeAngularConicExtrusionSmallArcAndTwoPiSeam) {
    constexpr double halfWidth = 0x1p-10;
    const Plane source{Frame3()};
    for (bool ellipse : {false, true}) {
        const double a = 2., b = ellipse ? 1. : 2.;
        CurvePtr<3> profile;
        if (ellipse)
            profile = std::make_shared<Ellipse<3>>(Vec3(), Vec3(1., 0., 0.), Vec3(0., 1., 0.), a, b);
        else
            profile = std::make_shared<Circle<3>>(Vec3(), Vec3(1., 0., 0.), Vec3(0., 1., 0.), a);
        const ExtrusionSurface target(profile, Vec3(0., 0., 1.));
        for (double anchor : {0., kTwoPi}) for (double height : {0., .002}) {
            SurfaceWrapOptions options;
            options.anchor = {anchor, 0.};
            options.normalOffset = height;
            const auto image = angularConicTaylorImage(a, b, height);
            const auto result = bound(target, image, source, options, budget,
                {-halfWidth, halfWidth}, {-2., 3.});
            certified(result);
            for (int i = 0; i <= 16; ++i) for (double v : {-2., .5, 3.}) {
                const double u = -halfWidth + 2. * halfWidth * i / 16.;
                const Vec3 expected(a * std::cos(u), b * std::sin(u), v);
                const Vec3 normal = normalized(Vec3(b * std::cos(u), a * std::sin(u), 0.));
                FK_CHECK(distance(target.point(anchor + u, v), expected) <= 3e-15);
                FK_CHECK(distance(target.normal(anchor + u, v), normal) <= 3e-15);
                const double measured = distance(image.point(u, v), expected + height * normal);
                FK_CHECK(measured <= result.upperBound);
                FK_CHECK(measured < budget / 100.);
            }
            // The Taylor error is below 1e-8; a 3e-6 displacement cannot
            // fit the external 1e-6 budget, including at the native seam.
            FK_CHECK(bound(target, angularConicTaylorImage(a, b, height, 3. * budget), source,
                options, budget, {-halfWidth, halfWidth}, {-2., 3.}).status == Status::Failed);
            if (height != 0.)
                FK_CHECK(bound(target, angularConicTaylorImage(a, b, -height), source,
                    options, budget, {-halfWidth, halfWidth}, {-2., 3.}).status == Status::Failed);
        }
    }
}

FK_TEST(MappingBoundsAngularConicObliqueQuarterTurnWithInclinedExtrusion) {
    // A bilinear tangent approximation over a small oblique rectangle. Its
    // error on the ellipse is bounded analytically by max(a,b) * delta^2/2.
    constexpr double half = 0x1p-12;
    const Plane source{Frame3()};
    const Vec3 direction(.6, 0., .8);
    const ExtrusionSurface target(std::make_shared<Ellipse<3>>(Vec3(), Vec3(1, 0, 0), Vec3(0, 1, 0), 3., 2.), direction);
    SurfaceWrapOptions options;
    options.anchor = {-3. * kHalfPi, 1.};
    options.angle = std::atan2(3., 4.);
    std::vector<Vec3> poles;
    for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) {
        const double s = half * (2. * i / 3. - 1.), t = half * (2. * j / 3. - 1.);
        poles.push_back(Vec3(-3. * (.8 * s - .6 * t), 2., 0.) + (1. + .6 * s + .8 * t) * direction);
    }
    const auto knots = clamped(3, -half, half);
    const BSplineSurface image(3, 3, knots, knots, 4, 4, poles);
    const auto result = bound(target, image, source, options, budget, {-half, half}, {-half, half});
    certified(result);
    const double deviationY = 4. * std::pow(std::sin(.7 * half), 2);
    FK_CHECK(deviationY > 1e-8 && deviationY <= result.upperBound);
    FK_CHECK(bound(target, image, source, options, 1e-8, {-half, half}, {-half, half}).status == Status::Failed);
}

FK_TEST(MappingBoundsAngularConicExtrusionRequiresDefinedNormalAtSeam) {
    // At U=0 the tangent of the XY circle is parallel to the Y extrusion.
    // The zero-height wrap still calls Surface::normal at that interior point.
    constexpr double half = 0x1p-10;
    const ExtrusionSurface target(std::make_shared<Circle<3>>(Vec3(), Vec3(1, 0, 0), Vec3(0, 1, 0), 1.), Vec3(0, 1, 0));
    const Plane source{Frame3()};
    std::vector<Vec3> poles;
    for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j)
        poles.emplace_back(1., half * (2. * i / 3. - 1.) + j / 3., 0.);
    const BSplineSurface image(3, 3, clamped(3, -half, half), clamped(3), 4, 4, poles);
    // The geometric position error alone is within budget (< half^2/2 plus
    // the cubic sine error). The singular tangent makes the wrap undefined.
    FK_CHECK(bound(target, image, source, {}, budget, {-half, half}).status == Status::Failed);
}

FK_TEST(MappingBoundsTrimmedAngularEllipseCrossesTwoPiWithoutWrappingItsOpenDomain) {
    constexpr double halfWidth = 0x1p-10;
    const auto ellipse = std::make_shared<Ellipse<3>>(Vec3(), Vec3(1., 0., 0.), Vec3(0., 1., 0.), 2., 1.);
    const auto arc = std::make_shared<TrimmedCurve<3>>(ellipse,
        kTwoPi - halfWidth / 2., kTwoPi + halfWidth / 2.);
    const ExtrusionSurface target(arc, Vec3(0., 0., 1.));
    FK_CHECK(!target.isUPeriodic());
    const Plane source{Frame3()};
    SurfaceWrapOptions options;
    options.anchor = {kTwoPi, 0.};
    options.normalOffset = .002;
    const auto image = angularConicTaylorImage(2., 1., options.normalOffset);
    certified(bound(target, image, source, options, budget,
        {-halfWidth / 2., halfWidth / 2.}, {-2., 3.}));
    for (double u : {-halfWidth / 2., 0., halfWidth / 2.}) {
        const Vec3 expected(2. * std::cos(u), std::sin(u), .5);
        FK_CHECK(distance(target.point(kTwoPi + u, .5), expected) <= 3e-15);
    }
    FK_CHECK(bound(target, image, source, options, budget,
        {-halfWidth, halfWidth}, {-2., 3.}).status == Status::Failed);
    // A full turn is not an admissible substitute for this exposed open arc.
    options.anchor = {0., 0.};
    FK_CHECK(bound(target, image, source, options, budget,
        {-halfWidth / 2., halfWidth / 2.}, {-2., 3.}).status == Status::Failed);
}

FK_TEST(MappingBoundsNearlyClosedAngularTrimUsesItsShortExposedPeriod) {
    constexpr double period = 0x1p-26, anchor = .125;
    const auto ellipse = std::make_shared<Ellipse<3>>(Vec3(), Vec3(1., 0., 0.), Vec3(0., 1., 0.), 2., 1.);
    const auto arc = std::make_shared<TrimmedCurve<3>>(ellipse, 0., period);
    FK_CHECK(distance(arc->point(0.), arc->point(period)) < linearResolution());
    const ExtrusionSurface target(arc, Vec3(0., 0., 1.));
    FK_CHECK(target.isUPeriodic());
    FK_CHECK_NEAR(target.uPeriod(), period, 0.);
    const Plane source{Frame3()};
    SurfaceWrapOptions options;
    options.anchor = {anchor, 0.};
    const auto image = angularConicTaylorImage(2., 1., 0.);
    certified(bound(target, image, source, options, budget, {0., period / 2.}, {-2., 3.}));
    for (double u : {0., period / 4., period / 2.}) {
        const Vec3 expected(2. * std::cos(u), std::sin(u), .5);
        FK_CHECK(distance(target.point(anchor + u, .5), expected) <= 1e-15);
        // Reducing by the underlying ellipse period would leave anchor+u
        // unchanged and give a visibly different point on the full ellipse.
        const Vec3 wrong(2. * std::cos(anchor + u), std::sin(anchor + u), .5);
        FK_CHECK(distance(expected, wrong) > .1);
    }
    FK_CHECK(bound(target, angularConicTaylorImage(2., 1., 0., .001), source,
        options, budget, {0., period / 2.}, {-2., 3.}).status == Status::Failed);
}

FK_TEST(MappingBoundsAngularConicExtrusionInclinedLaminaAtZeroHeight) {
    constexpr double d = 0x1p-10;
    const Plane source{Frame3({}, Vec3(-.8, 0., .6), Vec3(.6, 0., .8))};
    const ExtrusionSurface target(std::make_shared<Ellipse<3>>(Vec3(), Vec3(1., 0., 0.), Vec3(0., 1., 0.), 2., 1.),
                                  Vec3(0., 0., 1.));
    const std::array<double, 4> linear{-d, -d/3., d/3., d};
    const std::array<double, 4> square{d*d, -d*d/3., -d*d/3., d*d};
    for (double sign : {1., -1.}) {
        // U=.6*s and h=.8*s. The first two Taylor orders of C+h*N
        // are X=2+.8*s-.36*s², Y=.6*s+.96*s². Flipping h changes the side.
        std::vector<Vec3> poles;
        for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j)
            poles.emplace_back(2. + sign * .8 * linear[i] - .36 * square[i],
                               .6 * linear[i] + sign * .96 * square[i], j/3.);
        const BSplineSurface image(3, 3, clamped(3, -d, d), clamped(3), 4, 4, poles);
        for (Interval range : {Interval{0., d}, Interval{-d, d}}) {
            const auto result = bound(target, image, source, {}, budget, range);
            if (sign < 0.) {
                FK_CHECK(result.status == Status::Failed);
                continue;
            }
            certified(result);
            for (double s : {range.lo, 0., range.hi}) {
                const double c = std::cos(.6*s), sn = std::sin(.6*s);
                const auto wanted = Vec3(2.*c, sn, .5) + (.8*s) * normalized(Vec3(c, 2.*sn, 0.));
                FK_CHECK(distance(image.point(s, .5), wanted) <= result.upperBound);
            }
        }
    }
}

FK_TEST(MappingBoundsTransformedAngularConicKeepsWorldExtrusionAndPhysicalNormal) {
    constexpr double half = 0x1p-10, height = .002;
    const Plane source{Frame3()};
    for (double sign : {-1., 1.}) {
        const auto ellipse = std::make_shared<Ellipse<3>>(Vec3(), Vec3(1., 0., 0.), Vec3(0., 1., 0.), 2., 1.);
        const auto innerTrim = std::make_shared<TrimmedCurve<3>>(ellipse, kTwoPi - half, kTwoPi + half);
        const auto shifted = std::make_shared<TransformedCurve>(innerTrim, Transform3::translation(Vec3(1., 2., 3.)));
        const auto outerTrim = std::make_shared<TrimmedCurve<3>>(shifted, kTwoPi - half / 2., kTwoPi + half / 2.);
        const double matrix[3][3]{{sign * 1.5, 0., 0.}, {0., 0., 2.}, {0., .5, 0.}};
        const auto scaled = std::make_shared<TransformedCurve>(outerTrim, Transform3::fromParts(matrix, Vec3()));
        const auto profile = std::make_shared<TransformedCurve>(scaled,
            Transform3::translation(Vec3(-sign * 1.5, -5., -1.)));
        // D belongs to the extrusion in world coordinates. Applying M to D
        // would instead extrude along Z and give a different surface/normal.
        const ExtrusionSurface target(profile, Vec3(0., 1., 0.));
        auto imageAtHeight = [&](double h) {
            const auto local = angularConicTaylorImage(3., .5, -sign * h);
            std::vector<Vec3> poles;
            for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) {
                const auto p = local.pole(i, j);
                poles.emplace_back(sign * p.x(), 1. + p.z(), p.y());
            }
            return BSplineSurface(3, 3, local.uKnots(), local.vKnots(), 4, 4, poles);
        };
        SurfaceWrapOptions options;
        options.anchor = {kTwoPi, 0.};
        options.normalOffset = height;
        const auto image = imageAtHeight(height);
        const auto result = bound(target, image, source, options, budget, {-half / 2., half / 2.}, {-2., 3.});
        certified(result);
        for (double u : {-half / 2., 0., half / 2.}) for (double v : {-2., .5, 3.}) {
            const Vec3 expected(sign * 3. * std::cos(u), 1. + v, .5 * std::sin(u));
            const Vec3 normal = normalized(Vec3(-std::cos(u), 0., -sign * 6. * std::sin(u)));
            FK_CHECK(distance(target.point(kTwoPi + u, v), expected) <= 5e-15);
            FK_CHECK(distance(target.normal(kTwoPi + u, v), normal) <= 5e-15);
            FK_CHECK(distance(image.point(u, v), expected + height * normal) <= result.upperBound);
        }
        FK_CHECK(bound(target, imageAtHeight(-height), source, options, budget,
            {-half / 2., half / 2.}, {-2., 3.}).status == Status::Failed);
        FK_CHECK(bound(target, image, source, options, budget,
            {-half, half}, {-2., 3.}).status == Status::Failed);
    }
}

FK_TEST(MappingBoundsAngularConicTranslationCancellationIncludesIntermediateRounding) {
    constexpr double translation = 0x1p30, lost = 0x1p-24, half = 0x1p-10;
    const auto ellipse = std::make_shared<Ellipse<3>>(Vec3(lost, 0., 0.),
        Vec3(1., 0., 0.), Vec3(0., 1., 0.), 2., 1.);
    const auto shifted = std::make_shared<TransformedCurve>(ellipse,
        Transform3::translation(Vec3(translation, 0., 0.)));
    const auto restored = std::make_shared<TransformedCurve>(shifted,
        Transform3::translation(Vec3(-translation, 0., 0.)));
    const ExtrusionSurface target(restored, Vec3(0., 0., 1.));
    const auto image = angularConicTaylorImage(2., 1., 0., lost);
    const Plane source{Frame3()};
    // At U=0, C.x=2+2^-24. Adding 2^30 rounds away 2^-24;
    // subtracting 2^30 leaves exactly 2. Algebraic composition misses it.
    FK_CHECK(target.point(0., 0.).x() == 2.);
    FK_CHECK_NEAR(image.point(0., 0.).x(), 2. + lost, 1e-15);
    const double witness = distance(target.point(0., 0.), image.point(0., 0.));
    FK_CHECK(witness > .99 * lost);
    const auto result = bound(target, image, source, {}, 1e-4, {-half, half}, {-2., 3.});
    certified(result, 1e-4);
    FK_CHECK(result.upperBound >= witness);
    FK_CHECK(bound(target, image, source, {}, 1e-8, {-half, half}, {-2., 3.}).status == Status::Failed);
}

FK_TEST(MappingBoundsAngularTrimClosureUsesPhysicalNonuniformScale) {
    constexpr double period = 0x1p-20, anchor = .125;
    const auto ellipse = std::make_shared<Ellipse<3>>(Vec3(), Vec3(1., 0., 0.), Vec3(0., 1., 0.), 2., 1.);
    const auto arc = std::make_shared<TrimmedCurve<3>>(ellipse, 0., period);
    FK_CHECK(distance(arc->point(0.), arc->point(period)) > linearResolution());
    const Plane source{Frame3()};
    for (double scale : {1. / 16., 1.}) {
        const double matrix[3][3]{{1., 0., 0.}, {0., scale, 0.}, {0., 0., 1.}};
        const auto profile = std::make_shared<TransformedCurve>(arc, Transform3::fromParts(matrix, Vec3()));
        const ExtrusionSurface target(profile, Vec3(0., 0., 1.));
        const bool closes = scale < 1.;
        FK_CHECK(target.isUPeriodic() == closes);
        FK_CHECK((distance(profile->point(0.), profile->point(period)) <= linearResolution()) == closes);
        SurfaceWrapOptions options;
        options.anchor = {anchor, 0.};
        const auto result = bound(target, angularConicTaylorImage(2., scale, 0.), source,
            options, budget, {0., period / 4.}, {-2., 3.});
        const double u = period / 4.;
        if (closes) {
            certified(result);
            FK_CHECK_NEAR(target.uPeriod(), period, 0.);
            FK_CHECK(distance(target.point(anchor + u, .5),
                Vec3(2. * std::cos(u), scale * std::sin(u), .5)) <= 1e-15);
        } else {
            FK_CHECK(result.status == Status::Failed);
            FK_CHECK(distance(target.point(anchor + u, .5),
                Vec3(2. * std::cos(u), scale * std::sin(u), .5)) > .1);
        }
    }
}

FK_TEST(MappingBoundsSingularConicProjectionSeparatesRegularAndCollapsedNormal) {
    constexpr double half = 0x1p-10;
    const Plane source{Frame3()};
    const auto circle = std::make_shared<Circle<3>>(Vec3(), Vec3(1., 0., 0.), Vec3(0., 1., 0.), 2.);
    for (bool collapse : {false, true}) {
        // Both maps are singular. Projection onto XY preserves the conic
        // tangent; projection onto X collapses it at the interior U=0 seam.
        const double matrix[3][3]{{1., 0., 0.}, {0., collapse ? 0. : 1., 0.}, {0., 0., 0.}};
        const ExtrusionSurface target(std::make_shared<TransformedCurve>(circle,
            Transform3::fromParts(matrix, Vec3())), Vec3(0., 0., 1.));
        const auto local = angularConicTaylorImage(2., 2., 0.);
        std::vector<Vec3> poles;
        for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) {
            const auto p = local.pole(i, j);
            poles.emplace_back(p.x(), collapse ? 0. : p.y(), p.z());
        }
        const BSplineSurface image(3, 3, local.uKnots(), local.vKnots(), 4, 4, poles);
        const auto result = bound(target, image, source, {}, budget, {-half, half}, {-2., 3.});
        if (collapse) {
            std::array<Vec3, 4> derivatives;
            target.evaluate(0., .5, 1, derivatives.data());
            FK_CHECK(distance(derivatives[Surface::derivativeIndex(1, 0, 1)], Vec3()) == 0.);
            FK_CHECK(result.status == Status::Failed);
        } else {
            certified(result);
            FK_CHECK(distance(target.normal(0., .5), Vec3(1., 0., 0.)) <= 1e-15);
        }
    }
}

namespace {
std::array<double, 4> cubicHermitePoles(Interval range, double lo, double hi, double dlo, double dhi) {
    const double step = range.length() / 3.;
    return {lo, lo + step * dlo, hi - step * dhi, hi};
}

BSplineSurface radialHermiteImage(Interval u, Interval v,
        const std::array<double, 4> &radius, const std::array<double, 4> &height, const Vec3 &axis) {
    const auto cosine = cubicHermitePoles(u, std::cos(u.lo), std::cos(u.hi), -std::sin(u.lo), -std::sin(u.hi));
    const auto sine = cubicHermitePoles(u, std::sin(u.lo), std::sin(u.hi), std::cos(u.lo), std::cos(u.hi));
    std::vector<Vec3> poles;
    for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j)
        poles.push_back(axis + Vec3(radius[j] * cosine[i], radius[j] * sine[i], height[j]));
    return BSplineSurface(3, 3, clamped(3, u.lo, u.hi), clamped(3, v.lo, v.hi), 4, 4, poles);
}
}

FK_TEST(MappingBoundsConicRevolutionTangentsAtPoleAndPositionAcrossSeam) {
    constexpr double half = 0x1p-8, polarRadius = 2., wrongShift = 1e-4;
    const Plane source{Frame3()};
    const Vec3 axis(2., -1., 3.), z(0., 0., 1.), x(1., 0., 0.);
    const Interval u{-half, half}, v{-half, half}, regular{half / 2., half};
    for (double equatorialRadius : {polarRadius, 1.}) {
        CurvePtr<3> meridian;
        if (equatorialRadius == polarRadius)
            meridian = std::make_shared<Circle<3>>(axis, z, x, polarRadius);
        else
            meridian = std::make_shared<Ellipse<3>>(axis, z, x, polarRadius, equatorialRadius);
        const auto radius = cubicHermitePoles(v, equatorialRadius * std::sin(v.lo), equatorialRadius * std::sin(v.hi),
            equatorialRadius * std::cos(v.lo), equatorialRadius * std::cos(v.hi));
        const auto height = cubicHermitePoles(v, polarRadius * std::cos(v.lo), polarRadius * std::cos(v.hi),
            -polarRadius * std::sin(v.lo), -polarRadius * std::sin(v.hi));
        // The exact trigonometric factors have bounded fourth derivatives.
        // Endpoint Hermite interpolation incurs at most (2*half)^4 / 384
        // times the amplitude per factor, before coefficient roundoff.
        const auto image = radialHermiteImage(u, v, radius, height, axis);
        const auto wrong = radialHermiteImage(u, v, radius, height, axis + wrongShift * z);
        for (bool trimmed : {false, true}) {
            CurvePtr<3> profile = meridian;
            if (trimmed) profile = std::make_shared<TrimmedCurve<3>>(meridian, 0., .25);
            const RevolutionSurface target(profile, axis, z);
            const auto result = bound(target, image, source, {}, budget, u, regular);
            certified(result);
            std::array<Vec3, 4> derivatives;
            for (double theta : {u.lo, 0., u.hi}) for (double phi : {0., regular.lo, regular.hi}) {
                const Vec3 radial(std::cos(theta), std::sin(theta), 0.);
                const Vec3 azimuth(-std::sin(theta), std::cos(theta), 0.);
                const Vec3 position = axis + equatorialRadius * std::sin(phi) * radial + polarRadius * std::cos(phi) * z;
                const Vec3 tangentU = equatorialRadius * std::sin(phi) * azimuth;
                const Vec3 tangentV = equatorialRadius * std::cos(phi) * radial - polarRadius * std::sin(phi) * z;
                target.evaluate(theta, phi, 1, derivatives.data());
                FK_CHECK(distance(derivatives[0], position) < 1e-14);
                FK_CHECK(distance(derivatives[Surface::derivativeIndex(1, 0, 1)], tangentU) < 1e-14);
                FK_CHECK(distance(derivatives[Surface::derivativeIndex(0, 1, 1)], tangentV) < 1e-14);
                if (phi > 0.)
                    FK_CHECK(distance(image.point(theta, phi), position) <= result.upperBound + 1e-14);
            }
            FK_CHECK(distance(wrong.point(0., half), target.point(0., half)) > 10. * budget);
            FK_CHECK(bound(target, wrong, source, {}, budget, u, regular).status == Status::Failed);
            // wrapBodyOnSurface evaluates the natural normal even when h=0.
            FK_CHECK(bound(target, image, source, {}, budget, u, {0., half}).status == Status::Failed);
            const Interval negative{-half, -half / 2.};
            const auto below = bound(target, image, source, {}, budget, u, negative);
            if (trimmed)
                FK_CHECK(below.status == Status::Failed);
            else
                certified(below);
        }
    }
}

FK_TEST(MappingBoundsCanonicalConeNormalOffsetAcrossAngularSeam) {
    constexpr double half = 0x1p-8, radius = 2., angle = .5;
    const Plane source{Frame3()};
    const ConicalSurface target(Frame3(), angle, radius);
    const Interval u{-half, half}, v{0., 2. * half};
    const double sa = std::sin(angle), ca = std::cos(angle);
    const auto imageAt = [&](double h) {
        const auto radial = cubicHermitePoles(v, radius + h * ca, radius + v.hi * sa + h * ca, sa, sa);
        const auto axial = cubicHermitePoles(v, -h * sa, v.hi * ca - h * sa, ca, ca);
        return radialHermiteImage(u, v, radial, axial, {});
    };
    for (double h : {-.002, .002}) {
        SurfaceWrapOptions options;
        options.normalOffset = h;
        const auto image = imageAt(h), wrong = imageAt(-h);
        const auto result = bound(target, image, source, options, budget, u, v);
        certified(result);
        for (double theta : {u.lo, 0., u.hi}) for (double t : {v.lo, half, v.hi}) {
            const Vec3 radial(std::cos(theta), std::sin(theta), 0.);
            const Vec3 normal = ca * radial - Vec3(0., 0., sa);
            const Vec3 position = (radius + t * sa) * radial + Vec3(0., 0., t * ca);
            FK_CHECK(distance(target.point(theta, t), position) < 1e-14);
            FK_CHECK(distance(target.normal(theta, t), normal) < 1e-14);
            FK_CHECK(distance(image.point(theta, t), position + h * normal) <= result.upperBound + 1e-14);
        }
        const Vec3 edgeRadial(std::cos(u.lo), std::sin(u.lo), 0.);
        const auto edgePoint = (radius + half * sa + h * ca) * edgeRadial + Vec3(0., 0., half * ca - h * sa);
        FK_CHECK_NEAR(distance(wrong.point(u.lo, half), edgePoint),
            2. * std::abs(h), 1e-14);
        FK_CHECK(bound(target, wrong, source, options, budget, u, v).status == Status::Failed);
    }
}

FK_TEST(MappingBoundsSplineRevolutionPolynomialAndRationalMeridians) {
    constexpr double half = 0x1p-9;
    const Plane source{Frame3()};
    const Vec3 z(0., 0., 1.);
    const Interval u{-half, half}, v{.125, .125 + 2. * half};
    for (bool rational : {false, true}) {
        // For the rational case W=3+3v and H_z=12v. The meridian
        // has constant radius 2, but its axial parameter is nonlinear.
        const std::vector<Vec3> poles{{2., 0., 0.}, {2., 0., rational ? 1. : 1. / 3.},
            {2., 0., rational ? 1.6 : 2. / 3.}, {2., 0., rational ? 2. : 1.}};
        const auto profile = std::make_shared<BSplineCurve<3>>(3, clamped(3), poles,
            rational ? std::vector<double>{3., 4., 5., 6.} : std::vector<double>{});
        const RevolutionSurface target(profile, Vec3(), z);
        const auto axial = [&](double t) { return rational ? 4. * t / (1. + t) : t; };
        const auto axialDerivative = [&](double t) { return rational ? 4. / ((1. + t) * (1. + t)) : 1.; };
        const auto height = cubicHermitePoles(v, axial(v.lo), axial(v.hi), axialDerivative(v.lo), axialDerivative(v.hi));
        for (double h : {0., .002}) {
            SurfaceWrapOptions options;
            options.normalOffset = h;
            const double radius = 2. + h;
            const auto image = radialHermiteImage(u, v, {radius, radius, radius, radius}, height, {});
            const auto result = bound(target, image, source, options, budget, u, v);
            certified(result);
            for (double theta : {u.lo, 0., u.hi}) for (double t : {v.lo, v.lo + half, v.hi}) {
                const Vec3 radial(std::cos(theta), std::sin(theta), 0.);
                const auto exact = 2. * radial + axial(t) * z;
                FK_CHECK(distance(target.point(theta, t), exact) < 1e-14);
                FK_CHECK(distance(target.normal(theta, t), radial) < 1e-14);
                FK_CHECK(distance(image.point(theta, t), exact + h * radial) <= result.upperBound + 1e-14);
            }
        }
    }
}

FK_TEST(MappingBoundsSplineRevolutionFindsNarrowMeridianPeak) {
    constexpr double half = 0x1p-9, center = 3. / 16.;
    const Plane source{Frame3()};
    const Interval u{-half, half}, v{0., 1.};
    const Vec3 z(0., 0., 1.);
    const auto axial = cubicHermitePoles(v, 0., 1., 1., 1.);
    const auto image = radialHermiteImage(u, v, {2., 2., 2., 2.}, axial, {});
    const std::vector<double> knots{0., 0., 0., 0., 46. / 256., 47. / 256.,
        48. / 256., 49. / 256., 50. / 256., 1., 1., 1., 1.};
    for (double peak : {budget / 32., 1e-4}) {
        std::vector<Vec3> poles;
        for (std::size_t i = 0; i < 9; ++i)
            poles.emplace_back(2. + (i == 4 ? 1.5 * peak : 0.), 0.,
                (knots[i + 1] + knots[i + 2] + knots[i + 3]) / 3.);
        const auto profile = std::make_shared<BSplineCurve<3>>(3, knots, poles);
        const RevolutionSurface target(profile, Vec3(), z);
        // The central cardinal cubic contributes 2/3 at V=48/256
        // and zero outside [46/256, 50/256].
        for (double t : {0., .125, .25, .5, .75, 1.})
            FK_CHECK_NEAR(target.point(0., t).x(), 2., 1e-14);
        FK_CHECK_NEAR(target.point(0., center).x() - 2., peak, 1e-14);
        const auto result = bound(target, image, source, {}, budget, u, v);
        if (peak < budget) {
            certified(result);
            FK_CHECK(distance(target.point(0., center), image.point(0., center)) <= result.upperBound + 1e-14);
        } else {
            FK_CHECK(distance(target.point(0., center), image.point(0., center)) > 10. * budget);
            FK_CHECK(result.status == Status::Failed);
        }
    }
}

namespace {
BSplineSurface circularMeridianHermiteImage(Interval u, Interval v, double major, double minor) {
    const auto radius = cubicHermitePoles(v, major + minor * std::cos(v.lo), major + minor * std::cos(v.hi),
        -minor * std::sin(v.lo), -minor * std::sin(v.hi));
    const auto height = cubicHermitePoles(v, minor * std::sin(v.lo), minor * std::sin(v.hi),
        minor * std::cos(v.lo), minor * std::cos(v.hi));
    return radialHermiteImage(u, v, radius, height, {});
}
}

FK_TEST(MappingBoundsRotatedSphereUsesNaturalOffsetAndRejectsPoleAndLatitudeOutsideDomain) {
    constexpr double half = 0x1p-9, radius = 2., offset = .002;
    const Plane source{Frame3()};
    const Vec3 origin(3., -2., 5.);
    // Fixed right-handed permutation: local (x,y,z) becomes world (y,z,x).
    const auto rotated = [](const Vec3 &p) { return Vec3(p.y(), p.z(), p.x()); };
    const SphericalSurface target(Frame3::fromAxes(origin,
        Vec3(0., 0., 1.), Vec3(1., 0., 0.), Vec3(0., 1., 0.)), radius);
    const Interval u{-half, half}, regular{.125, .125 + 2. * half};
    const auto imageAt = [&](Interval v, double h) {
        const auto local = circularMeridianHermiteImage(u, v, 0., radius + h);
        std::vector<Vec3> poles;
        for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j)
            poles.push_back(origin + rotated(local.pole(i, j)));
        return BSplineSurface(3, 3, local.uKnots(), local.vKnots(), 4, 4, poles);
    };
    SurfaceWrapOptions options;
    options.normalOffset = offset;
    const auto image = imageAt(regular, offset);
    const auto result = bound(target, image, source, options, budget, u, regular);
    certified(result);
    for (double theta : {u.lo, 0., u.hi}) for (double latitude : {regular.lo, regular.lo + half, regular.hi}) {
        const Vec3 n = rotated(Vec3(std::cos(latitude) * std::cos(theta),
            std::cos(latitude) * std::sin(theta), std::sin(latitude)));
        FK_CHECK(distance(target.normal(theta, latitude), n) < 1e-14);
        FK_CHECK(distance(target.point(theta, latitude), origin + radius * n) < 1e-14);
        FK_CHECK(distance(image.point(theta, latitude), origin + (radius + offset) * n) <= result.upperBound + 1e-14);
    }
    FK_CHECK(bound(target, imageAt(regular, -offset), source, options, budget, u, regular).status == Status::Failed);

    // Both images describe their latitude intervals by the same formula.
    // The first reaches the singular north pole; the second lies strictly
    // outside the sphere's nonperiodic V domain, away from singularity.
    const Interval pole{kHalfPi - 2. * half, kHalfPi};
    const Interval outside{kHalfPi + .125, kHalfPi + .125 + 2. * half};
    FK_CHECK(distance(target.point(0., kHalfPi), origin + rotated(Vec3(0., 0., radius))) < 1e-14);
    FK_CHECK(bound(target, imageAt(pole, 0.), source, {}, budget, u, pole).status == Status::Failed);
    FK_CHECK(bound(target, imageAt(outside, 0.), source, {}, budget, u, outside).status == Status::Failed);
}

FK_TEST(MappingBoundsSpindleTorusUsesSignedRadiusOnInnerBranch) {
    constexpr double half = 0x1p-9, major = 1., minor = 2., offset = .002;
    const Plane source{Frame3()};
    const ToroidalSurface target(Frame3(), major, minor);
    const Interval u{-half, half}, v{kPi - half, kPi + half};
    SurfaceWrapOptions options;
    options.normalOffset = offset;
    // rho = R + r cos(v) < 0 on the inner branch, so Tu x Tv points
    // against (cos(v) cos(u), cos(v) sin(u), sin(v)).
    const auto image = circularMeridianHermiteImage(u, v, major, minor - offset);
    const auto wrong = circularMeridianHermiteImage(u, v, major, minor + offset);
    const auto result = bound(target, image, source, options, budget, u, v);
    certified(result);
    for (double theta : {u.lo, 0., u.hi}) for (double phi : {v.lo, kPi, v.hi}) {
        const Vec3 radial(std::cos(theta), std::sin(theta), 0.);
        const Vec3 n = -std::cos(phi) * radial - Vec3(0., 0., std::sin(phi));
        const Vec3 expected = major * radial - minor * n;
        FK_CHECK(major + minor * std::cos(phi) < 0.);
        FK_CHECK(distance(target.point(theta, phi), expected) < 1e-14);
        FK_CHECK(distance(target.normal(theta, phi), n) < 1e-14);
        FK_CHECK(distance(image.point(theta, phi), expected + offset * n) <= result.upperBound + 1e-14);
    }
    FK_CHECK_NEAR(distance(wrong.point(u.lo, v.lo), image.point(u.lo, v.lo)), 2. * offset, 1e-14);
    FK_CHECK(bound(target, wrong, source, options, budget, u, v).status == Status::Failed);
}

FK_TEST(MappingBoundsObliqueRevolutionWithHeightCrossingZero) {
    constexpr double half = 0x1p-16;
    const Plane source{Frame3({}, Vec3(-.8, 0., .6), Vec3(.6, 0., .8))};
    SurfaceWrapOptions options;
    options.angle = std::atan2(3., 4.);
    const ToroidalSurface target(Frame3(), 2., 1.);
    const Interval range{-half, half};
    for (double sign : {1., -1.}) {
        // First-order Taylor image at the outer equator. The source height
        // is .8*s, and both angular parameters change along both source axes.
        std::vector<Vec3> poles;
        for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) {
            const double s = half * (2. * i / 3. - 1.), t = half * (2. * j / 3. - 1.);
            poles.emplace_back(3. + sign * .8*s, 3.*(.48*s - .6*t), .36*s + .8*t);
        }
        const auto knots = clamped(3, range.lo, range.hi);
        const BSplineSurface image(3, 3, knots, knots, 4, 4, poles);
        const auto result = bound(target, image, source, options, budget, range, range);
        if (sign < 0.) {
            FK_CHECK(result.status == Status::Failed);
            continue;
        }
        certified(result);
        for (double s : {-half, 0., half}) for (double t : {-half, 0., half}) {
            const double u = .48*s - .6*t, v = .36*s + .8*t, h = .8*s;
            const Vec3 radial(std::cos(u), std::sin(u), 0.);
            const auto expected = (2. + (1.+h)*std::cos(v))*radial + Vec3(0., 0., (1.+h)*std::sin(v));
            FK_CHECK(distance(image.point(s, t), expected) <= result.upperBound);
        }
    }
}
