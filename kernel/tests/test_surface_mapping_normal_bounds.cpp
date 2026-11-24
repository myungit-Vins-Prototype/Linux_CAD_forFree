#include "fk_bspline_surface.h"
#include "fk_cancel.h"
#include "fk_surface_mapping.h"
#include "fk_surface_mapping_bounds.h"
#include "fk_test.h"

#include <atomic>
#include <cmath>
#include <limits>
#include <memory>
#include <vector>

using namespace ForgeCad::Kernel;
namespace {
using Status = detail::SurfaceMappingBoundStatus;
constexpr double budget = 1e-6;

std::vector<double> normalKnots(int degree) {
    std::vector<double> knots(degree + 1, 0.);
    knots.insert(knots.end(), degree + 1, 1.);
    return knots;
}

BSplineSurface tiltedTarget(int degree, bool variableWeights = false) {
    std::vector<Vec3> poles;
    std::vector<double> weights;
    for (int i = 0; i <= degree; ++i) for (int j = 0; j <= degree; ++j) {
        double u = double(i) / degree;
        const double v = double(j) / degree;
        if (variableWeights) {
            const double weight = 1. + .5 * i / degree;
            weights.push_back(weight);
            u = (u + .5 * i * (i - 1) / (degree * (degree - 1))) / weight;
        }
        poles.emplace_back(u, v, .75 * u);
    }
    return BSplineSurface(degree, degree, normalKnots(degree), normalKnots(degree),
        degree + 1, degree + 1, poles, weights);
}

BSplineSurface tiltedImage(double height, bool swapped) {
    std::vector<Vec3> poles;
    for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) {
        const double u = (swapped ? j : i) / (3. * 1.25);
        const double v = (swapped ? i : j) / 3.;
        // T(U,V)=(U,V,.75U), Tu x Tv=(-.75,0,1), norm=1.25.
        // The target's NATURAL normal is (-.6,0,.8), independently of the
        // sign of the source-to-target UV permutation determinant.
        poles.emplace_back(u - .6 * height, v, .75 * u + .8 * height);
    }
    return BSplineSurface(3, 3, normalKnots(3), normalKnots(3), 4, 4, poles);
}

Plane parallelSource(double height, bool swapped) {
    if (swapped)
        return Plane(Frame3::fromAxes(Vec3(0, 0, height), Vec3(0, 1, 0),
            Vec3(1, 0, 0), Vec3(0, 0, -1)));
    return Plane(Frame3(Vec3(0, 0, height), Vec3(0, 0, 1), Vec3(1, 0, 0)));
}

void normalCertified(const detail::SurfaceMappingBoundResult &proof) {
    FK_CHECK(proof.status == Status::Certified);
    FK_CHECK(std::isfinite(proof.upperBound) && proof.upperBound >= 0 && proof.upperBound <= budget);
}

detail::SurfaceMappingBoundResult normalBound(const Surface &source, const Surface &target,
        const Surface &image, SurfaceWrapOptions options = {}, double scaleU = 1.25) {
    return detail::planarMappingResidualBound(source, target, options,
        scaleU, 1, image, {0, 1}, {0, 1}, budget);
}
}

FK_TEST(MappingNormalBoundsTiltedSignedHeightAndSwappedUVMatchExactCubic) {
    for (int degree : {1, 2, 3, 4, 5}) for (double height : {.002, -.002}) {
        const auto target = tiltedTarget(degree);
        const auto image = tiltedImage(height, false);
        const auto source = parallelSource(height, false);
        normalCertified(normalBound(source, target, image));
        // Independent affine oracle, not target.normal(): at source(.5,.5)
        // U=.4, V=.5 and normal displacement is height*(-.6,0,.8).
        FK_CHECK(distance(image.point(.5, .5),
            Vec3(.4 - .6 * height, .5, .3 + .8 * height)) <= 1e-15);

        // Equivalent constant height supplied by options, with det(UV)<0.
        // Crossing composed derivatives would reverse N and fail this case.
        auto options = SurfaceWrapOptions{};
        options.normalOffset = height;
        const auto swappedSource = parallelSource(0, true);
        const auto swappedImage = tiltedImage(height, true);
        normalCertified(normalBound(swappedSource, target, swappedImage, options));
        FK_CHECK(distance(swappedImage.point(.25, .75),
            Vec3(.6 - .6 * height, .25, .45 + .8 * height)) <= 1e-15);
    }
}

FK_TEST(MappingBoundsQuinticShapeAndDegreeBudget) {
    // On this (5,4) Bezier patch the last row contributes exactly delta*u^5.
    constexpr double delta = 3e-7;
    std::vector<Vec3> poles;
    for (int i = 0; i <= 5; ++i) for (int j = 0; j <= 4; ++j)
        poles.emplace_back(i / 5., j / 4., i == 5 ? delta : 0.);
    const BSplineSurface target(5, 4, normalKnots(5), normalKnots(4), 6, 5, poles);
    std::vector<Vec3> flat;
    for (int i = 0; i <= 3; ++i) for (int j = 0; j <= 3; ++j)
        flat.emplace_back(i / 3., j / 3., 0.);
    const BSplineSurface image(3, 3, normalKnots(3), normalKnots(3), 4, 4, flat);
    const Plane source{Frame3()};
    const auto proof = normalBound(source, target, image, {}, 1.);
    normalCertified(proof);
    FK_CHECK(proof.upperBound >= delta);
    FK_CHECK_NEAR(target.point(.5, .5).z(), delta / 32., 1e-20);
    FK_CHECK(detail::planarMappingResidualBound(source, target, {}, 1., 1., image,
        {0, 1}, {0, 1}, delta / 2.).status == Status::Failed);
    // Recognized B-splines outside the polynomial work budget fail terminally.
    FK_CHECK(normalBound(source, tiltedTarget(21), image).status == Status::Failed);
}

FK_TEST(MappingNormalBoundsWrongOrientedSideFailsIncludingNegativeHeight) {
    const auto target = tiltedTarget(3);
    for (double height : {.002, -.002}) {
        const auto source = parallelSource(height, false);
        const auto wrong = tiltedImage(-height, false);
        const Vec3 correct(.4 - .6 * height, .5, .3 + .8 * height);
        FK_CHECK_NEAR(distance(wrong.point(.5, .5), correct), 2 * std::fabs(height), 1e-15);
        // The unoriented normal-line and distance-squared invariants both
        // vanish here. The sign must still reject the opposite normal side.
        FK_CHECK(normalBound(source, target, wrong).status == Status::Failed);
    }
}

FK_TEST(MappingNormalBoundsMissedInteriorBumpAtNonzeroHeightFails) {
    const std::vector<double> knots{0, 0, 0, 0, 46. / 256., 47. / 256.,
        48. / 256., 49. / 256., 50. / 256., 1, 1, 1, 1};
    std::vector<Vec3> poles;
    for (int i = 0; i < 9; ++i) for (int j = 0; j < 9; ++j)
        poles.emplace_back((knots[i + 1] + knots[i + 2] + knots[i + 3]) / 3.,
            (knots[j + 1] + knots[j + 2] + knots[j + 3]) / 3.,
            i == 4 && j == 4 ? (9. / 4.) * 1e-4 : 0.);
    const BSplineSurface target(3, 3, knots, knots, 9, 9, poles);
    std::vector<Vec3> flatPoles;
    for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j)
        flatPoles.emplace_back(i / 3., j / 3., .002);
    const BSplineSurface image(3, 3, normalKnots(3), normalKnots(3), 4, 4, flatPoles);
    const auto source = parallelSource(.002, false);
    // B4(c)^2=4/9 and its gradient is zero at c=3/16: adding h*N leaves
    // the exact positional gap at this witness equal to 1e-4, not h noise.
    FK_CHECK_NEAR(target.point(3. / 16., 3. / 16.).z(), 1e-4, 1e-16);
    FK_CHECK(normalBound(source, target, image, {}, 1).status == Status::Failed);
}

FK_TEST(MappingNormalBoundsDegenerateAndNearlySingularNormalsFail) {
    std::vector<Vec3> poles;
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j)
        poles.emplace_back(i / 2., 0, 0);
    const BSplineSurface degenerate(2, 2, normalKnots(2), normalKnots(2), 3, 3, poles);
    const auto source = parallelSource(.002, false);
    const auto image = tiltedImage(.002, false);
    FK_CHECK(normalBound(source, degenerate, image, {}, 1).status == Status::Failed);

    // Nonzero mathematical cross product is not sufficient: the existing
    // Surface::normal metric rejects T=(u,1e-15*v,0) as nearly singular.
    std::vector<Vec3> squashedPoles, squashedImagePoles;
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j)
        squashedPoles.emplace_back(i / 2., 1e-15 * (j / 2.), 0);
    for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j)
        squashedImagePoles.emplace_back(i / 3., 1e-15 * (j / 3.), .002);
    const BSplineSurface squashed(2, 2, normalKnots(2), normalKnots(2), 3, 3, squashedPoles);
    const BSplineSurface squashedImage(3, 3, normalKnots(3), normalKnots(3), 4, 4, squashedImagePoles);
    FK_CHECK(normalBound(source, squashed, squashedImage, {}, 1).status == Status::Failed);
}

FK_TEST(MappingNormalBoundsPresetCancellationRemainsDistinct) {
    const auto source = parallelSource(-.002, true);
    const auto target = tiltedTarget(2), image = tiltedImage(-.002, true);
    const auto token = std::make_shared<std::atomic<bool>>(true);
    bool cancelled = false;
    {
        CancellationScope scope(token);
        try {
            (void)normalBound(source, target, image);
        } catch (const OperationCancelled &) {
            cancelled = true;
        }
    }
    FK_CHECK(cancelled);
}

FK_TEST(MappingNormalBoundsAffineHeightRetainsItsSignAndNaturalNormal) {
    const auto target = tiltedTarget(2);
    for (bool swapped : {false, true}) for (double h0 : {.125, -1.}) {
        const Vec3 sloped(-.8, 0, .6);
        const Vec3 straight(0, 1, 0);
        const auto x = swapped ? straight : sloped;
        const auto y = swapped ? sloped : straight;
        const Plane source(Frame3::fromAxes(Vec3(1, 0, h0), x, y, cross(x, y)));
        const auto image = [&](double side) {
            std::vector<Vec3> poles;
            for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) {
                const double t = (swapped ? j : i) / 3.;
                const double v = (swapped ? i : j) / 3.;
                const double u = (1. - .8 * t) / 1.25;
                const double h = side * (h0 + .6 * t);
                poles.emplace_back(u - .6 * h, v, .75 * u + .8 * h);
            }
            return BSplineSurface(3, 3, normalKnots(3), normalKnots(3), 4, 4, poles);
        };
        normalCertified(normalBound(source, target, image(1)));
        FK_CHECK(normalBound(source, target, image(-1)).status == Status::Failed);
    }
}

FK_TEST(MappingNormalBoundsAtAndAcrossZeroAllowSmallOffsetError) {
    const auto target = tiltedTarget(2);
    for (double h0 : {0., -.3}) for (bool swapped : {false, true}) {
        const Vec3 sloped(-.8, 0, .6), straight(0, 1, 0);
        const auto x = swapped ? straight : sloped;
        const auto y = swapped ? sloped : straight;
        const Plane source(Frame3::fromAxes(Vec3(1, 0, h0), x, y, cross(x, y)));
        const auto image = [&](double delta, double side) {
            std::vector<Vec3> poles;
            for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) {
                const double t = (swapped ? j : i) / 3.;
                const double v = (swapped ? i : j) / 3.;
                const double u = (1. - .8 * t) / 1.25;
                // For delta != 0 the fitted normal displacement changes
                // sign at a different parameter than the requested height.
                const double h = side * (h0 + .6 * t) + delta;
                poles.emplace_back(u - .6 * h, v, .75 * u + .8 * h);
            }
            return BSplineSurface(3, 3, normalKnots(3), normalKnots(3), 4, 4, poles);
        };
        const auto accepted = normalBound(source, target, image(budget / 4., 1));
        normalCertified(accepted);
        FK_CHECK(accepted.upperBound >= budget / 4. - 1e-15);
        FK_CHECK(normalBound(source, target, image(0, -1)).status == Status::Failed);
    }
}

FK_TEST(MappingNormalBoundsInvalidCoefficientsFailRatherThanFallBack) {
    const auto target = tiltedTarget(2), image = tiltedImage(.002, false);
    const auto source = parallelSource(.002, false);
    FK_CHECK(normalBound(source, target, image, {}, std::numeric_limits<double>::denorm_min()).status
        == Status::Failed);

    // Cancellation makes the interval for the constant height straddle zero.
    // An oblique UV map must not mask that constant-height arithmetic failure.
    auto options = SurfaceWrapOptions{};
    options.sourcePlane = Frame3(Vec3(0, 0, std::numeric_limits<double>::denorm_min()),
        Vec3(0, 0, 1), Vec3(1, 0, 0));
    options.normalOffset = -1;
    options.angle = .2;
    FK_CHECK(normalBound(parallelSource(1, false), target, image, options).status == Status::Failed);
}


FK_TEST(MappingNormalBoundsRankOneSupportsBothAxesAndNaturalNormalOnKnot) {
    const std::vector<double> knots{0, 0, 0, 0, .5, .5, 1, 1, 1, 1};
    std::vector<Vec3> targetPoles;
    for (int i = 0; i < 6; ++i) for (int j = 0; j < 6; ++j) {
        const double u = (knots[i + 1] + knots[i + 2] + knots[i + 3]) / 3.;
        const double v = (knots[j + 1] + knots[j + 2] + knots[j + 3]) / 3.;
        targetPoles.emplace_back(u, v, .75 * u);
    }
    const BSplineSurface target(3, 3, knots, knots, 6, 6, targetPoles);
    for (bool constantU : {false, true}) for (bool heightAlongU : {false, true})
        for (double rate : {-1., 1.}) for (double startHeight : {-.5, .002}) {
            const double start = rate < 0 ? 1. : 0.;
            const Vec3 along = constantU ? Vec3(0, rate, 0) : Vec3(rate, 0, 0);
            const Vec3 up(0, 0, 1);
            const auto x = heightAlongU ? up : along;
            const auto y = heightAlongU ? along : up;
            const Plane source(Frame3::fromAxes(Vec3(constantU ? .5 : start, constantU ? start : .5, startHeight),
                x, y, cross(x, y)));
            std::vector<Vec3> poles;
            for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) {
                const double active = start + rate * (heightAlongU ? j : i) / 3.;
                const double h = startHeight + (heightAlongU ? i : j) / 3.;
                const double u = constantU ? .5 : active;
                const double v = constantU ? active : .5;
                poles.emplace_back(u - .6 * h, v, .75 * u + .8 * h);
            }
            const BSplineSurface image(3, 3, normalKnots(3), normalKnots(3), 4, 4, poles);
            normalCertified(normalBound(source, target, image, {}, 1.));
        }
}

FK_TEST(MappingNormalBoundsDiagonalRankOneUsesBothSourceAxesAndNaturalNormal) {
    const auto target = tiltedTarget(3);
    for (bool alongU : {false, true}) for (bool reverse : {false, true})
        for (double startHeight : {.002, -.5}) {
            const Vec3 origin(reverse ? .8 : .1, reverse ? .1 : .9, startHeight);
            const Vec3 travel(reverse ? -.6 : .6, reverse ? .8 : -.8, 0);
            const Vec3 up(0, 0, 1);
            const auto x = alongU ? travel : up, y = alongU ? up : travel;
            const Plane source(Frame3::fromAxes(origin, x, y, cross(x, y)));
            const auto image = [&](double side) {
                std::vector<Vec3> poles;
                for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) {
                    const double t = (alongU ? i : j) / 3.;
                    const double h = side * (startHeight + (alongU ? j : i) / 3.);
                    const double u = origin.x() + t * travel.x();
                    const double v = origin.y() + t * travel.y();
                    poles.emplace_back(u - .6 * h, v, .75 * u + .8 * h);
                }
                return BSplineSurface(3, 3, normalKnots(3), normalKnots(3), 4, 4, poles);
            };
            normalCertified(normalBound(source, target, image(1), {}, 1.));
            FK_CHECK(normalBound(source, target, image(-1), {}, 1.).status == Status::Failed);
        }
}

FK_TEST(MappingNormalBoundsObliqueUncertainRankRetainsNaturalNormal) {
    // The interval for the XY determinant contains zero. A reversal changes
    // the source frame and the height field, but the target normal is fixed.
    const Vec3 x(.36, .48, .8), y(.48, .64, -.6);
    for (int degree : {2, 3}) for (bool reverse : {false, true})
        for (double height : {-.1, .75}) {
            const auto target = tiltedTarget(degree);
            const Vec3 origin(.1, .12, height);
            const auto axis = reverse ? -x : x;
            const Plane source(Frame3::fromAxes(origin + (reverse ? x : Vec3()), axis, y, cross(axis, y)));
            const auto oracle = [&](double s, double t, double offset) {
                const Vec3 p = origin + (reverse ? 1. - s : s) * x + t * y;
                const double u = .5 * p.x(), v = .5 * p.y();
                return Vec3(u, v, .75 * u) + (p.z() + offset) * Vec3(-.6, 0, .8);
            };
            for (double offset : {2e-8, 1e-4}) {
                std::vector<Vec3> poles;
                for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j)
                    poles.push_back(oracle(i / 3., j / 3., offset));
                const BSplineSurface image(3, 3, normalKnots(3), normalKnots(3), 4, 4, poles);
                const auto proof = detail::planarMappingResidualBound(source, target, {}, 2., 2.,
                    image, {0., 1.}, {0., 1.}, budget);
                const double measured = distance(image.point(.25, .75), oracle(.25, .75, 0.));
                FK_CHECK_NEAR(measured, offset, 1e-15);
                if (offset > budget) FK_CHECK(proof.status == Status::Failed);
                else {
                    normalCertified(proof);
                    FK_CHECK(proof.upperBound + 1e-15 >= measured);
                }
            }
        }
}

FK_TEST(MappingNormalBoundsObliqueKeepsNaturalNormalAcrossParameterReversal) {
    const Vec3 along(.6, .8, 0), across(.8, -.6, 0);
    constexpr double offset = .002, error = 2e-8;
    for (int degree : {2, 3}) for (bool reverse : {false, true}) {
        const auto target = tiltedTarget(degree);
        const Vec3 start = Vec3(.3, .8, offset) + (reverse ? along : Vec3());
        const Vec3 x = reverse ? -along : along;
        const Plane source(Frame3::fromAxes(start, x, across, cross(x, across)));
        const auto oracle = [&](double s, double t, double height) {
            // U=.15+.3*s+.4*t and V=.4+.4*s-.3*t before the reversal.
            // The natural target normal (-.6,0,.8) is independent of it.
            const double r = reverse ? 1. - s : s;
            const double u = .15 + .3 * r + .4 * t, v = .4 + .4 * r - .3 * t;
            return Vec3(u - .6 * height, v, .75 * u + .8 * height);
        };
        std::vector<Vec3> poles;
        for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j)
            poles.push_back(oracle(i / 3., j / 3., offset + error));
        const BSplineSurface image(3, 3, normalKnots(3), normalKnots(3), 4, 4, poles);
        const auto proof = detail::planarMappingResidualBound(source, target, {}, 2., 2.,
            image, {0., 1.}, {0., 1.}, budget);
        normalCertified(proof);
        const double measured = distance(image.point(.25, .75), oracle(.25, .75, offset));
        FK_CHECK_NEAR(measured, error, 1e-15);
        FK_CHECK(proof.upperBound + 1e-15 >= measured);
    }
}

FK_TEST(MappingNormalBoundsDiagonalRankOneRetainsCorrelatedQuadraticTerm) {
    const Vec3 start(.2, .9, -5e-9);
    const Vec3 travel(.6, -.8, 0), up(0, 0, 1);
    const Plane source(Frame3::fromAxes(start, travel, up, cross(travel, up)));
    constexpr double scale = .32, height = 1e-8;
    const std::vector<double> thinKnots{0., 0., 0., 0., height, height, height, height};
    for (int degree : {2, 3}) {
        std::vector<Vec3> targetPoles;
        for (int i = 0; i <= degree; ++i) for (int j = 0; j <= degree; ++j) {
            const double u = double(i) / degree, v = double(j) / degree;
            targetPoles.emplace_back(u, v, scale * u * v);
        }
        const BSplineSurface target(degree, degree, normalKnots(degree), normalKnots(degree),
            degree + 1, degree + 1, targetPoles);
        for (bool linearized : {false, true}) {
            std::vector<Vec3> poles;
            for (int i = 0; i < 4; ++i) {
                const double t = i / 3.;
                const double tSquared = linearized ? t : i * (i - 1) / 6.;
                // T(u,v).z = scale*u*v; on the diagonal the t^2 term
                // must survive. The thin source bounds the normal shift by 5e-9.
                const double uv = start.x() * start.y() + (start.x() * travel.y() + start.y() * travel.x()) * t
                    + travel.x() * travel.y() * tSquared;
                for (int j = 0; j < 4; ++j)
                    poles.emplace_back(start.x() + t * travel.x(), start.y() + t * travel.y(), scale * uv);
            }
            const BSplineSurface image(3, 3, normalKnots(3), thinKnots, 4, 4, poles);
            const auto proof = detail::planarMappingResidualBound(source, target, {}, 1., 1.,
                image, {0., 1.}, {0., height}, budget);
            if (linearized) FK_CHECK(proof.status == Status::Failed);
            else normalCertified(proof);
        }
    }
}

FK_TEST(MappingNormalBoundsObliqueRetainsMixedBernsteinTerm) {
    const Vec3 origin(.3, .8, 0), x(.6, .8, 0), y(.8, -.6, 0);
    const Plane source(Frame3::fromAxes(origin, x, y, cross(x, y)));
    constexpr double scale = .32;
    for (int degree : {2, 3}) {
        std::vector<Vec3> targetPoles;
        for (int i = 0; i <= degree; ++i) for (int j = 0; j <= degree; ++j) {
            const double u = double(i) / degree, v = double(j) / degree;
            targetPoles.emplace_back(u, v, scale * u * v);
        }
        const BSplineSurface target(degree, degree, normalKnots(degree), normalKnots(degree),
            degree + 1, degree + 1, targetPoles);
        for (bool omitMixedTerm : {false, true}) {
            std::vector<Vec3> poles;
            for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) {
                const double s = i / 3., t = j / 3.;
                // U=.15+.3s+.4t, V=.4+.4s-.3t. Expand U*V and
                // convert each source monomial to cubic Bernstein poles.
                const double mixed = omitMixedTerm ? 0. : .07 * s * t;
                const double uv = .06 + .18 * s + .115 * t
                    + .12 * (i * (i - 1) / 6.) - .12 * (j * (j - 1) / 6.) + mixed;
                poles.emplace_back(.15 + .3 * s + .4 * t, .4 + .4 * s - .3 * t, scale * uv);
            }
            const BSplineSurface image(3, 3, normalKnots(3), normalKnots(3), 4, 4, poles);
            const auto proof = detail::planarMappingResidualBound(source, target, {}, 2., 2.,
                image, {0., 1.}, {0., 1.}, budget);
            if (omitMixedTerm) FK_CHECK(proof.status == Status::Failed);
            else normalCertified(proof);
        }
    }
}

FK_TEST(MappingNormalBoundsReflectedTargetRetainsNaturalNormal) {
    const double matrix[3][3]{{-1., 0., 0.}, {0., 1., 0.}, {0., 0., 1.}};
    const Vec3 translation(.125, -.25, .5);
    const auto placement = Transform3::fromParts(matrix, translation);
    for (int degree : {2, 3}) for (bool variableWeights : {false, true}) {
        const auto base = std::make_shared<BSplineSurface>(tiltedTarget(degree, variableWeights));
        const BSplineSurface target(base, placement);
        for (bool swapped : {false, true}) for (double height : {-.002, 0., .002}) {
            // height==0 selects an affine source height crossing zero.
            const auto source = height == 0.
                ? Plane(Frame3(Vec3(0, 0, -.001), Vec3(-.002, 0, 1), Vec3(1, 0, .002)))
                : parallelSource(height, swapped);
            FK_CHECK(distance(target.normal(.375, .625), Vec3(-.6, 0, -.8)) <= 1e-14);
            if (height == 0.)
                FK_CHECK(source.point(0, 0).z() < 0. && source.point(1, 0).z() > 0.);
            for (bool wrongSide : {false, true}) {
                const auto oracle = [&](double s, double t) {
                    const Vec3 point = source.point(s, t);
                    const double u = point.x() / 1.25, v = point.y();
                    // The reflection gives T=(-U,V,.75U)+translation,
                    // with natural unit normal (-.6,0,-.8).
                    const double h = wrongSide ? -point.z() : point.z();
                    return translation + Vec3(-u - .6 * h, v, .75 * u - .8 * h);
                };
                std::vector<Vec3> poles;
                for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j)
                    poles.push_back(oracle(i / 3., j / 3.));
                const BSplineSurface image(3, 3, normalKnots(3), normalKnots(3), 4, 4, poles);
                const auto proof = normalBound(source, target, image);
                if (wrongSide) FK_CHECK(proof.status == Status::Failed);
                else normalCertified(proof);
            }
        }
    }
}

FK_TEST(MappingNormalBoundsInstancedTargetKeepsNativeSubUlpTranslation) {
    const double translation = std::ldexp(1., 18);
    const double shift = std::ldexp(1., -36);
    std::vector<Vec3> poles;
    for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j)
        poles.emplace_back(i / 3. + shift, j / 3., 0.);
    const auto base = std::make_shared<BSplineSurface>(3, 3, normalKnots(3), normalKnots(3), 4, 4, poles);
    const BSplineSurface target(base, Transform3::translation(Vec3(translation, 0, 0)));
    for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j)
        poles[4 * i + j] = target.pole(i, j);
    const BSplineSurface flattened(3, 3, normalKnots(3), normalKnots(3), 4, 4, poles);
    // The rounded world pole has lost the native contribution at the corner.
    // Comparing only rounded world poles would make this residual zero.
    FK_CHECK(flattened.pole(0, 0).x() == translation);
    const Plane source(Frame3{});
    const auto prove = [&](double cap) {
        return detail::planarMappingResidualBound(source, target, {}, 1., 1.,
            flattened, {0., 1.}, {0., 1.}, cap);
    };
    FK_CHECK(prove(.5 * shift).status == Status::Failed);
    const auto coarse = prove(1e-8);
    FK_CHECK(coarse.status == Status::Certified && coarse.upperBound >= shift && coarse.upperBound <= 1e-8);
}
