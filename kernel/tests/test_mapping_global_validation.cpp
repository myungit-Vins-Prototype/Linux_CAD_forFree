#include "fk_body_check.h"
#include "fk_body_io.h"
#include "fk_bspline_surface.h"
#include "fk_classify_uv.h"
#include "fk_history.h"
#include "fk_planar.h"
#include "fk_primitives.h"
#include "fk_sheet.h"
#include "fk_surface_mapping.h"
#include "fk_surface_mapping_bounds.h"
#include "fk_test.h"

#include <algorithm>
#include <array>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace ForgeCad::Kernel;

namespace {
constexpr double tolerance = 1e-6;
constexpr double center = 3. / 16.;
constexpr double peakPole = (9. / 4.) * 1e-4;

BSplineSurface localizedCarrier(bool bump) {
    const std::vector<double> knots{0, 0, 0, 0, 46. / 256., 47. / 256.,
        48. / 256., 49. / 256., 50. / 256., 1, 1, 1, 1};
    std::vector<Vec3> poles;
    for (int i = 0; i < 9; ++i) {
        const double x = (knots[i + 1] + knots[i + 2] + knots[i + 3]) / 3.;
        for (int j = 0; j < 9; ++j) {
            const double y = (knots[j + 1] + knots[j + 2] + knots[j + 3]) / 3.;
            poles.emplace_back(x, y, bump && i == 4 && j == 4 ? peakPole : 0.);
        }
    }
    return BSplineSurface(3, 3, knots, knots, 9, 9, poles);
}

void unchangedCarrier(const BSplineSurface &actual, const BSplineSurface &before) {
    FK_CHECK(!actual.basis() && !before.basis());
    FK_CHECK(actual.uDegree() == before.uDegree() && actual.vDegree() == before.vDegree());
    FK_CHECK(actual.uKnots() == before.uKnots() && actual.vKnots() == before.vKnots());
    FK_CHECK(actual.uPoleCount() == before.uPoleCount() && actual.vPoleCount() == before.vPoleCount());
    for (int i = 0; i < before.uPoleCount(); ++i) for (int j = 0; j < before.vPoleCount(); ++j) {
        const Vec3 a = actual.pole(i, j), b = before.pole(i, j);
        for (int k = 0; k < 3; ++k) FK_CHECK(a[k] == b[k]);
        FK_CHECK(actual.weight(i, j) == before.weight(i, j));
    }
}

std::string historyStamp(const BodyHistory &history) {
    std::ostringstream out;
    out << history.operandCount();
    for (auto kind : {EntityKind::Vertex, EntityKind::Edge, EntityKind::Face}) {
        out << ':' << history.resultSize(kind);
        for (int i = 0; i < history.resultSize(kind); ++i) {
            out << '/' << history.isDeclared(kind, i);
            for (const auto &origin : history.origins(kind, i))
                out << ',' << origin.operand << ',' << int(origin.kind) << ',' << origin.index
                    << ',' << int(origin.relation);
        }
    }
    return out.str();
}

void checkFaceOrigins(const Body &source, const Body &result, const BodyHistory &history) {
    FK_CHECK(history.operandCount() == 1);
    for (auto face : result.faces()) {
        FK_CHECK(history.isDeclared(EntityKind::Face, face.index));
        const auto &origins = history.origins(EntityKind::Face, face.index);
        FK_CHECK(origins.size() == 1);
        if (origins.size() != 1) continue;
        const auto &origin = origins.front();
        FK_CHECK(origin.operand == 0 && origin.kind == EntityKind::Face
            && origin.relation == HistoryRelation::Modified);
        FK_CHECK(source.contains(FaceId{origin.index}));
    }
}

SurfaceWrapOptions wrapOptions() {
    SurfaceWrapOptions options;
    options.anchor = {0, 0};
    options.tolerance = tolerance;
    return options;
}
}

FK_TEST(MappingGlobalNativeBumpHasIndependentInteriorWitness) {
    const auto target = localizedCarrier(true);
    // Cardinal cubic B_4 has knots 46..50 / 256. At its center 48/256,
    // its value is (4 - 6*0^2 + 3*0^3)/6 = 2/3, its derivative is zero.
    // Tensor-product height is therefore 4/9 of the ONLY nonzero Z pole.
    // This oracle does not evaluate the production basis or mapping callback.
    const double expectedHeight = (4. / 9.) * peakPole;
    FK_CHECK_NEAR(expectedHeight, 1e-4, 1e-19);
    const Vec3 witness(center, center, expectedHeight);
    FK_CHECK(distance(target.point(center, center), witness) <= 1e-15);
    FK_CHECK(distance(target.normal(center, center), Vec3(0, 0, 1)) <= 1e-12);
    // The peak's entire support lies strictly inside the box footprint;
    // its boundary is flat, so correct edge/fin checks cannot detect its loss.
    for (double t : {0., center, 1.}) {
        FK_CHECK(target.point(0, t).z() == 0. && target.point(1, t).z() == 0.);
        FK_CHECK(target.point(t, 0).z() == 0. && target.point(t, 1).z() == 0.);
    }
}

FK_TEST(MappingGlobalFlatCarrierWithSameKnotsSucceeds) {
    const auto source = makeBox(Frame3(), 1, 1, .001);
    const auto sourceBefore = writeBodyBinary(source);
    const auto target = localizedCarrier(false), targetBefore = target;
    BodyHistory history;
    double deviation = -123;
    const auto options = wrapOptions();
    // Exceptions are failures: this control prevents blanket rejection of
    // valid native carriers merely because they have narrow knot spans.
    const auto result = wrapBodyOnSurface(source, target, options, &history, &deviation);
    FK_CHECK(!result.isSheet() && result.faces().size() == source.faces().size());
    FK_CHECK(checkBody(result, {true, true}).empty());
    FK_CHECK(std::isfinite(deviation) && deviation >= 0 && deviation <= tolerance);
    checkFaceOrigins(source, result, history);
    for (auto vertex : result.vertices()) {
        const auto &origins = history.origins(EntityKind::Vertex, vertex.index);
        FK_CHECK(history.isDeclared(EntityKind::Vertex, vertex.index) && origins.size() == 1);
        if (origins.size() != 1) continue;
        FK_CHECK(origins.front().operand == 0 && origins.front().kind == EntityKind::Vertex);
        const VertexId parent{origins.front().index};
        FK_CHECK(source.contains(parent));
        if (source.contains(parent))
            FK_CHECK(distance(result.vertex(vertex).point, source.vertex(parent).point) <= tolerance);
    }
    FK_CHECK(writeBodyBinary(result) == writeBodyBinary(wrapBodyOnSurface(source, target, options, nullptr)));
    FK_CHECK(writeBodyBinary(source) == sourceBefore);
    unchangedCarrier(target, targetBefore);
}

FK_TEST(MappingGlobalSolidBumpRejectsSpecificUnprovenCarrierBeforePublication) {
    const auto source = makeBox(Frame3(), 1, 1, .001);
    const auto sourceBefore = writeBodyBinary(source);
    const auto target = localizedCarrier(true), targetBefore = target;
    auto history = sameIndexHistory(source, source);
    const auto oldHistory = historyStamp(history);
    double deviation = -123;
    bool rejected = false;
    try {
        (void)wrapBodyOnSurface(source, target, wrapOptions(), &history, &deviation);
    } catch (const std::domain_error &error) {
        rejected = true;
        FK_CHECK(std::string(error.what()).find("residuo globale del carrier") != std::string::npos);
    }
    // An unrelated exception escapes to the runner and fails this test.
    FK_CHECK(rejected);
    FK_CHECK(deviation == -123 && historyStamp(history) == oldHistory);
    FK_CHECK(writeBodyBinary(source) == sourceBefore);
    unchangedCarrier(target, targetBefore);
}

FK_TEST(MappingGlobalZeroHeightBottomIdentityRetainsTheRealBump) {
    const auto source = makePlaneSheet(Frame3(Vec3(.5, .5, 0), Vec3(0, 0, 1), Vec3(1, 0, 0)), .5);
    const auto sourceBefore = writeBodyBinary(source);
    const auto target = localizedCarrier(true), targetBefore = target;
    BodyHistory history;
    double deviation = -123;
    const auto options = wrapOptions();
    // A sheet at zero normal height has an exact carrier route. It is a
    // positive reference for the solid's bottom, not a fitted solid control.
    const auto result = wrapBodyOnSurface(source, target, options, &history, &deviation);
    FK_CHECK(result.isSheet() && result.faces().size() == 1);
    FK_CHECK(checkBody(result, {true, true}).empty());
    FK_CHECK(std::isfinite(deviation) && deviation >= 0 && deviation <= tolerance);
    checkFaceOrigins(source, result, history);
    if (result.faces().size() == 1) {
        const auto face = result.faces().front();
        FK_CHECK(distance(result.face(face).surface->point(center, center),
            Vec3(center, center, (4. / 9.) * peakPole)) <= tolerance);
        FK_CHECK(FaceParameterClassifier(result, face, tolerance).classify({center, center})
            == PointLocation::Inside);
    }
    FK_CHECK(writeBodyBinary(result) == writeBodyBinary(wrapBodyOnSurface(source, target, options, nullptr)));
    FK_CHECK(writeBodyBinary(source) == sourceBefore);
    unchangedCarrier(target, targetBefore);
}


FK_TEST(MappingGlobalPeriodicNativeBumpRejectsBeforePublicationWithFlatControl) {
    const auto source = makeBox(Frame3(), .75, .75, .001);
    const auto sourceBefore = writeBodyBinary(source);
    for (bool alongU : {true, false}) for (bool bump : {false, true}) {
        const auto native = localizedCarrier(bump);
        std::vector<Vec3> poles;
        for (int i = 0; i < native.uPoleCount(); ++i)
            for (int j = 0; j < native.vPoleCount(); ++j) poles.push_back(native.pole(i, j));
        const BSplineSurface target(3, 3, native.uKnots(), native.vKnots(),
            native.uPoleCount(), native.vPoleCount(), poles, {}, alongU, !alongU);
        auto history = sameIndexHistory(source, source);
        const auto oldHistory = historyStamp(history);
        double deviation = -123;
        bool rejected = false;
        try {
            const auto result = wrapBodyOnSurface(source, target, wrapOptions(), &history, &deviation);
            FK_CHECK(!bump);
            FK_CHECK(checkBody(result, {true, true}).empty());
            FK_CHECK(std::isfinite(deviation) && deviation >= 0 && deviation <= tolerance);
            checkFaceOrigins(source, result, history);
        } catch (const std::domain_error &error) {
            rejected = true;
            FK_CHECK(bump);
            FK_CHECK(std::string(error.what()).find("residuo globale del carrier") != std::string::npos);
        }
        FK_CHECK(rejected == bump);
        if (bump) FK_CHECK(deviation == -123 && historyStamp(history) == oldHistory);
        FK_CHECK(writeBodyBinary(source) == sourceBefore);
        unchangedCarrier(target, native);
        FK_CHECK(target.isUPeriodic() == alongU && target.isVPeriodic() != alongU);
    }
}

namespace {
Body nonlinearProjectionQuilt(double bump, bool bothAxes = false, bool threePatches = false, double shear = 0.) {
    const auto graph = [bothAxes, shear](double lo, double hi, double relief) {
        std::vector<double> u{lo, lo, lo}, v{-2., -2., -2.};
        if (lo == 0.)
            u.insert(u.end(), {1200. / 4096., 1240. / 4096., 1256. / 4096., 1257. / 4096.});
        // At the shear fixture's anchor, a one-sided knot evaluates the
        // exact Cartesian contact frame. The two sides use the same polynomial.
        if (shear != 0.) v.insert(v.end(), {0., 0.});
        if (lo == 0. || shear != 0.)
            v.insert(v.end(), {640. / 4096., 680. / 4096., 696. / 4096., 697. / 4096.});
        u.insert(u.end(), 3, hi);
        v.insert(v.end(), 3, 2.);
        std::vector<Vec3> poles;
        for (std::size_t i = 0; i + 3 < u.size(); ++i) {
            for (std::size_t j = 0; j + 3 < v.size(); ++j) {
                // The projected abscissa is x=u+u*u/1024. The seam is x=0.
                const double uu = u[i + 1] * u[i + 2];
                poles.emplace_back(.5 * (u[i + 1] + u[i + 2]) + uu / 1024. + shear * .5 * (v[j + 1] + v[j + 2]),
                    .5 * (v[j + 1] + v[j + 2]) + (bothAxes ? v[j + 1] * v[j + 2] / 1024. : 0.),
                    uu / 512. + (i == 3 && j == (shear != 0. ? 5u : 3u) ? relief : 0.));
            }
        }
        return std::make_shared<BSplineSurface>(2, 2, u, v, u.size() - 3, v.size() - 3, poles);
    };
    const auto cuts = threePatches ? std::vector<double>{-2., -.5, 0., 2.}
                                  : std::vector<double>{-2., 0., 2.};
    const int count = int(cuts.size()) - 1;
    const auto surfaceAt = [count](int i) { return std::min(i, count - 1); };
    std::vector<std::shared_ptr<BSplineSurface>> surfaces;
    for (int i = 0; i < count; ++i)
        surfaces.push_back(graph(cuts[i], cuts[i + 1], i == count - 1 ? bump : 0.));
    std::vector<Vec3> points;
    for (double v : {-2., 2.}) for (int i = 0; i <= count; ++i)
        points.push_back(surfaces[surfaceAt(i)]->point(cuts[i], v));
    std::vector<Body::BuildEdge> edges;
    for (int row = 0; row < 2; ++row) for (int i = 0; i < count; ++i)
        edges.push_back({row * (count + 1) + i, row * (count + 1) + i + 1,
            surfaces[i]->vIso(row == 0 ? -2. : 2.), {cuts[i], cuts[i + 1]}, 0});
    for (int i = 0; i <= count; ++i)
        edges.push_back({i, i + count + 1, surfaces[surfaceAt(i)]->uIso(cuts[i]), {-2., 2.}, 0});
    const auto horizontal = [](double v) {
        return std::make_shared<Line<2>>(Vec2(0., v), Vec2(1., 0.));
    };
    const auto vertical = [](double u) {
        return std::make_shared<Line<2>>(Vec2(u, 0.), Vec2(0., 1.));
    };
    std::vector<Body::BuildFace> faces;
    for (int i = 0; i < count; ++i) {
        Body::BuildFace face;
        face.surface = surfaces[i];
        face.loops = {{{i, true, horizontal(-2.), 0}, {2 * count + i + 1, true, vertical(cuts[i + 1]), 0},
                       {count + i, false, horizontal(2.), 0}, {2 * count + i, false, vertical(cuts[i]), 0}}};
        faces.push_back(std::move(face));
    }
    auto quilt = Body::buildSheet(points, edges, faces);
    if (!checkBody(quilt, {true, true}).empty())
        throw std::domain_error("invalid nonlinear projection fixture");
    return quilt;
}
}

FK_TEST(MappingGlobalNonlinearProjectionOnTwoAndThreeCurvedPatches) {
    const auto source = makePlaneSheet(Frame3(), 1.);
    const auto sourceBefore = writeBodyBinary(source);
    const auto sourceFace = source.faces().front();
    auto options = wrapOptions();
    options.angle = .4;
    options.shift = {.05, -.08};
    const double budget = .2 * std::min(options.tolerance, linearResolution());
    // The central piece of the localized quadratic basis has its extremum
    // at t=17/73. Construct the physical projection independently of Newton.
    constexpr double t = 17. / 73.;
    constexpr double u = (1240. + 16. * t) / 4096., v = (680. + 16. * t) / 4096.;
    constexpr double weight = (40. + 16. * t) / 56. * (1. - t) + (17. - 16. * t) / 17. * t;
    const double c = std::cos(options.angle), s = std::sin(options.angle);
    const auto sourceAt = [&](double x, double y) {
        const auto xy = Vec2(x, y) - options.shift;
        return Vec2(c * xy.x() + s * xy.y(), -s * xy.x() + c * xy.y());
    };
    struct Trial { bool threePatches, bothAxes; double shear; };
    for (const Trial &trial :
         {Trial{false, false, 0.}, {false, true, 0.}, {true, false, 0.}, {true, true, 0.}, {true, true, .25}})
    for (double bump : {0., 1e-8, -1e-8, 2.5e-8, -4.4e-8}) {
        const bool threePatches = trial.threePatches, bothAxes = trial.bothAxes;
        const double shear = trial.shear;
        const auto projectedY = [bothAxes](double v) { return v + (bothAxes ? v * v / 1024. : 0.); };
        const auto quilt = nonlinearProjectionQuilt(bump, bothAxes, threePatches, shear);
        const auto targetBefore = writeBodyBinary(quilt);
        const auto faces = quilt.faces();
        std::vector<SurfaceTargetFace> targets;
        for (auto face : faces) targets.push_back({&quilt, face});
        if (faces.size() == 3) std::swap(targets[0], targets[1]);
        const auto reliefFace = faces.back();
        FK_CHECK(faces.size() == (threePatches ? 3u : 2u));
        // All source material stays at zero normal height (a foil, no solid
        // thickness). The three-patch control crosses BOTH shared curved seams.
        for (std::size_t i = 1; i < faces.size(); ++i) {
            const double seam = threePatches && i == 1 ? -.5 : 0.;
            const auto &left = *quilt.face(faces[i - 1]).surface;
            const auto &right = *quilt.face(faces[i]).surface;
            for (double y : {-1., 0., 1.}) {
                FK_CHECK(distance(left.point(seam, y), right.point(seam, y)) < 1e-14);
                FK_CHECK(distance(left.normal(seam, y), right.normal(seam, y)) < 1e-12);
            }
        }

        const auto peak = Vec3(u + u * u / 1024. + shear * v, projectedY(v), u * u / 512. + bump * weight * weight);
        FK_CHECK(distance(peak, quilt.face(reliefFace).surface->point(u, v)) < 1e-14);
        const auto where = sourceAt(peak.x(), peak.y());
        FK_CHECK(FaceParameterClassifier(source, sourceFace, 1e-8).classify(where) == PointLocation::Inside);
        FK_CHECK(FaceParameterClassifier(quilt, reliefFace, 1e-8).classify(Vec2(u, v)) == PointLocation::Inside);
        auto history = sameIndexHistory(source, source);
        const auto oldHistory = historyStamp(history);
        double deviation = -123.;
        const bool mustReject = std::abs(bump) > 2e-8 && !(shear != 0. && bump > 0.);
        bool rejected = false;
        try {
            const auto result = wrapBodyOnFaces(source, targets, options, &history, &deviation);
            FK_CHECK(!mustReject);
            FK_CHECK(result.isSheet() && result.faces().size() == 1);
            FK_CHECK(checkBody(result, {true, true}).empty());
            checkFaceOrigins(source, result, history);
            FK_CHECK(std::isfinite(deviation) && deviation >= 0. && deviation <= options.tolerance);
            if (result.faces().size() != 1) continue;
            const auto &image = *result.face(result.faces().front()).surface;
            if (bump == 0.) {
                const auto active = detail::projectedMappingResidualBound(source, sourceFace, targets, options,
                    Frame3(), image, {-1., 1.}, {-1., 1.}, budget);
                FK_CHECK(active.status == detail::SurfaceMappingBoundStatus::Certified);
                auto disjoint = options;
                disjoint.shift = {10., 0.};
                const auto proof = detail::projectedMappingResidualBound(source, sourceFace, targets, disjoint,
                    Frame3(), image, {-1., 1.}, {-1., 1.}, budget);
                FK_CHECK(proof.status == detail::SurfaceMappingBoundStatus::Unsupported);
                if (shear != 0.) {
                    const auto other = nonlinearProjectionQuilt(0., true, true, .125);
                    auto incompatible = targets;
                    incompatible.back() = {&other, other.faces().back()};
                    const auto unsupported = detail::projectedMappingResidualBound(source, sourceFace, incompatible,
                        options, Frame3(), image, {-1., 1.}, {-1., 1.}, budget);
                    FK_CHECK(unsupported.status == detail::SurfaceMappingBoundStatus::Unsupported);
                }
            }
            const double peakResidual = distance(image.point(where.x(), where.y()), peak);
            FK_CHECK(peakResidual <= budget && peakResidual <= deviation + 1e-12);
            for (double a : {-.75, -.375, 0., .375, .75}) for (double b : {-.5, 0., .5}) {
                const Vec3 expected(a + a * a / 1024. + shear * b, projectedY(b), a * a / 512.);
                const auto uv = sourceAt(expected.x(), expected.y());
                FK_CHECK(distance(image.point(uv.x(), uv.y()), expected) <= deviation + 1e-12);
            }
        } catch (const std::domain_error &error) {
            rejected = true;
            FK_CHECK(std::string(error.what()).find("residuo globale del carrier") != std::string::npos);
        }
        FK_CHECK(rejected == mustReject);
        if (rejected) FK_CHECK(deviation == -123. && historyStamp(history) == oldHistory);
        FK_CHECK(writeBodyBinary(source) == sourceBefore && writeBodyBinary(quilt) == targetBefore);
    }
}

namespace {
Body rectangularSplinePatch(const std::shared_ptr<BSplineSurface> &surface) {
    const auto [lo, hi] = surface->uDomain();
    const auto [bottom, top] = surface->vDomain();
    const std::vector<Vec3> points{surface->point(lo, bottom), surface->point(hi, bottom),
                                   surface->point(lo, top), surface->point(hi, top)};
    const std::vector<Body::BuildEdge> edges{
        {0, 1, surface->vIso(bottom), {lo, hi}, 0}, {1, 3, surface->uIso(hi), {bottom, top}, 0},
        {2, 3, surface->vIso(top), {lo, hi}, 0}, {0, 2, surface->uIso(lo), {bottom, top}, 0}};
    const auto horizontal = [](double y) {
        return std::make_shared<Line<2>>(Vec2(0., y), Vec2(1., 0.));
    };
    const auto vertical = [](double x) {
        return std::make_shared<Line<2>>(Vec2(x, 0.), Vec2(0., 1.));
    };
    Body::BuildFace face;
    face.surface = surface;
    face.loops = {{{0, true, horizontal(bottom), 0}, {1, true, vertical(hi), 0},
                   {2, false, horizontal(top), 0}, {3, false, vertical(lo), 0}}};
    return Body::buildSheet(points, edges, {face});
}

Body exactRectangularSource(const Frame3 &frame) {
    ProfileRegion profile;
    const std::array<Vec2, 4> corners{{{-1., -1.}, {1., -1.}, {1., 1.}, {-1., 1.}}};
    for (std::size_t i = 0; i < corners.size(); ++i)
        profile.outer.segments.push_back({std::make_shared<Line<2>>(corners[i], corners[(i + 1) % 4] - corners[i]), {0., 2.}});
    return planarSheet(frame, {profile});
}

Body rationalGraphPanel(double lo, double hi, double relief, bool rational, double middleShift = 0.) {
    const std::vector<double> ku{lo, lo, lo, hi, hi, hi}, kv{-2., -2., 2., 2.};
    std::vector<Vec3> poles;
    std::vector<double> weights;
    for (int i = 0; i < 3; ++i) for (double y : {-2., 2.}) {
        const double x = i == 0 ? lo : (i == 2 ? hi : (rational ? .5 + middleShift : .5 * (lo + hi)));
        poles.emplace_back(x, y, i == 2 ? relief : 0.);
        if (rational) weights.push_back(1. + i);
    }
    const auto surface = std::make_shared<BSplineSurface>(2, 1, ku, kv, 3, 2, poles, weights);
    return rectangularSplinePatch(surface);
}
}

FK_TEST(MappingGlobalRationalTrimmedGraphsWithAffineAndNonlinearProjection) {
    const auto source = makePlaneSheet(Frame3(), 1.);
    const auto left = rationalGraphPanel(-2., 0., 0., false);
    const auto options = wrapOptions();
    const double budget = .2 * std::min(options.tolerance, linearResolution());
    const std::vector<double> k{-1., -1., -1., -1., 1., 1., 1., 1.};
    std::vector<Vec3> poles;
    for (double x : {-1., -1./3., 1./3., 1.}) for (double y : {-1., -1./3., 1./3., 1.})
        poles.emplace_back(x, y, 0.);
    const BSplineSurface flat(3, 3, k, k, 4, 4, poles);
    const auto nonMonotone = rationalGraphPanel(0., 2., 0., true, 2.);
    const auto unsupported = detail::projectedMappingResidualBound(source, source.faces().front(),
        {{&left, left.faces().front()}, {&nonMonotone, nonMonotone.faces().front()}},
        options, Frame3(), flat, {-1., 1.}, {-1., 1.}, budget);
    FK_CHECK(unsupported.status == detail::SurfaceMappingBoundStatus::Unsupported);
    for (double warp : {0., .125}) for (double relief : {4e-9, 1e-7}) {
        const auto right = rationalGraphPanel(0., 2., relief, true, warp);
        FK_CHECK(checkBody(left, {true, true}).empty() && checkBody(right, {true, true}).empty());
        const auto &graph = *right.face(right.faces().front()).surface;
        const auto expected = graph.point(.875, -.25);
        const double t = .875 / 2.;
        const double height = 3. * relief * t * t / (1. + 2. * t);
        const double x = .875 + 4. * warp * t * (1. - t) / (1. + 2. * t);
        FK_CHECK(distance(expected, Vec3(x, -.25, height)) < 1e-14);
        const std::vector<SurfaceTargetFace> targets{{&left, left.faces().front()}, {&right, right.faces().front()}};
        const auto proof = detail::projectedMappingResidualBound(source, source.faces().front(), targets,
            options, Frame3(), flat, {-1., 1.}, {-1., 1.}, budget);
        if (height > budget) {
            FK_CHECK(proof.status == detail::SurfaceMappingBoundStatus::Failed);
            continue;
        }
        FK_CHECK(proof.status == detail::SurfaceMappingBoundStatus::Certified &&
                 proof.upperBound >= height && proof.upperBound <= budget);
        double deviation = -1.;
        BodyHistory history;
        const auto result = wrapBodyOnFaces(source, targets, options, &history, &deviation);
        FK_CHECK(result.isSheet() && result.faces().size() == 1 && checkBody(result, {true, true}).empty());
        checkFaceOrigins(source, result, history);
        const auto &image = *result.face(result.faces().front()).surface;
        FK_CHECK(distance(expected, image.point(x, -.25)) <= deviation + 1e-12);
    }
}

FK_TEST(MappingGlobalCoupledGraphsWithDistinctTransverseProjections) {
    const auto patch = [](double bottom, double top, double shear) {
        const std::vector<double> ku{-2., -2., 2., 2.}, kv{bottom, bottom, bottom, top, top, top};
        std::vector<Vec3> poles;
        for (double u : {-2., 2.}) for (int j = 0; j < 3; ++j) {
            const double a = kv[j + 1], b = kv[j + 2], v2 = a * b;
            poles.emplace_back(u + shear * v2, .5 * (a + b), v2 / 512.);
        }
        return rectangularSplinePatch(std::make_shared<BSplineSurface>(1, 2, ku, kv, 2, 3, poles));
    };
    const auto lower = patch(-2., 0., .125);
    const auto upper = patch(0., 2., -.1875);
    FK_CHECK(checkBody(lower, {true, true}).empty() && checkBody(upper, {true, true}).empty());
    const auto source = makePlaneSheet(Frame3(), 1.);
    const auto sourceFace = source.faces().front();
    const std::vector<SurfaceTargetFace> targets{{&lower, lower.faces().front()}, {&upper, upper.faces().front()}};
    const auto options = wrapOptions();
    const double budget = .2 * std::min(options.tolerance, linearResolution());
    BodyHistory history;
    double deviation = -1.;
    const auto body = wrapBodyOnFaces(source, targets, options, &history, &deviation);
    FK_CHECK(body.isSheet() && body.faces().size() == 1 && checkBody(body, {true, true}).empty());
    checkFaceOrigins(source, body, history);
    const auto &image = *body.face(body.faces().front()).surface;
    const auto proof = detail::projectedMappingResidualBound(source, sourceFace, targets, options,
        Frame3(), image, {-1., 1.}, {-1., 1.}, budget);
    FK_CHECK(proof.status == detail::SurfaceMappingBoundStatus::Certified && proof.upperBound <= budget);
    for (double v : {-.75, -.25, 0., .25, .75}) {
        const Vec3 expected(.25, v, v * v / 512.);
        FK_CHECK(distance(image.point(.25, v), expected) <= deviation + 1e-12);
    }
}

FK_TEST(MappingGlobalCoincidentProjectedOverlap) {
    const auto source = makePlaneSheet(Frame3(), 1.);
    const auto sourceFace = source.faces().front();
    const auto left = rationalGraphPanel(-2., .5, 0., false);
    auto options = wrapOptions();
    options.normalOffset = .000125;
    const double budget = .2 * std::min(options.tolerance, linearResolution());
    for (bool rational : {false, true}) {
        const auto right = rationalGraphPanel(-.5, 2., 0., rational, .125);
        const std::vector<SurfaceTargetFace> targets{{&left, left.faces().front()}, {&right, right.faces().front()}};
        BodyHistory history;
        double deviation = -1.;
        const auto result = wrapBodyOnFaces(source, targets, options, &history, &deviation);
        FK_CHECK(result.isSheet() && result.faces().size() == 1 && checkBody(result, {true, true}).empty());
        if (result.faces().size() != 1) continue;
        checkFaceOrigins(source, result, history);
        const auto &image = *result.face(result.faces().front()).surface;
        const auto proof = detail::projectedMappingResidualBound(source, sourceFace, targets, options,
            Frame3(), image, {-1., 1.}, {-1., 1.}, budget);
        FK_CHECK(proof.status == detail::SurfaceMappingBoundStatus::Certified && proof.upperBound <= budget);
        FK_CHECK(proof.upperBound <= deviation);
        FK_CHECK(distance(image.point(.25, -.5), Vec3(.25, -.5, options.normalOffset)) <= deviation + 1e-12);

        const auto raised = rationalGraphPanel(-.5, 2., .01, rational, .125);
        const std::vector<SurfaceTargetFace> conflicting{{&left, left.faces().front()}, {&raised, raised.faces().front()}};
        const auto uncertain = detail::projectedMappingResidualBound(source, sourceFace, conflicting, options,
            Frame3(), image, {-1., 1.}, {-1., 1.}, budget);
        FK_CHECK(uncertain.status == detail::SurfaceMappingBoundStatus::Unsupported);
        bool rejected = false;
        try {
            (void)wrapBodyOnFaces(source, conflicting, options);
        } catch (const std::domain_error &error) {
            rejected = std::string(error.what()).find("sovrapposte lungo la normale") != std::string::npos;
        }
        FK_CHECK(rejected);
    }
}

FK_TEST(MappingGlobalCurvedCoincidentProjectedOverlap) {
    const auto panel = [](double lo, double hi, bool rational, bool split, double warp, double heightShift) {
        const std::vector<double> ku = rational ? std::vector<double>{lo, lo, lo, hi, hi, hi}
                                               : std::vector<double>{lo, lo, hi, hi};
        const std::vector<double> kv = split ? std::vector<double>{-2., -2., -2., 0., 2., 2., 2.}
                                               : std::vector<double>{-2., -2., -2., 2., 2., 2.};
        std::vector<Vec3> poles;
        std::vector<double> weights;
        const int nu = rational ? 3 : 2, nv = split ? 4 : 3;
        for (int i = 0; i < nu; ++i) for (int j = 0; j < nv; ++j) {
            const double a = kv[j + 1], b = kv[j + 2];
            const double x = i == 0 ? lo : (i + 1 == nu ? hi : lo + (hi - lo) / 4. + warp);
            poles.emplace_back(x, .5 * (a + b), a * b / 512. + heightShift);
            if (rational) weights.push_back(1. + i);
        }
        return rectangularSplinePatch(std::make_shared<BSplineSurface>(nu - 1, 2, ku, kv, nu, nv, poles, weights));
    };
    const auto source = makePlaneSheet(Frame3(), 1.);
    const auto sourceFace = source.faces().front();
    const auto options = wrapOptions();
    const double budget = .2 * std::min(options.tolerance, linearResolution());
    const std::array<std::array<double, 2>, 3> warps{{{0., 0.}, {0., .125}, {.0625, .125}}};
    for (const auto &[leftWarp, rightWarp] : warps) {
        const auto left = panel(-3., 1., leftWarp != 0., false, leftWarp, 0.);
        const auto right = panel(-1., 3., true, true, rightWarp, 0.);
        FK_CHECK(checkBody(left, {true, true}).empty() && checkBody(right, {true, true}).empty());
        const std::vector<SurfaceTargetFace> targets{{&left, left.faces().front()}, {&right, right.faces().front()}};
        BodyHistory history;
        double deviation = -1.;
        const auto result = wrapBodyOnFaces(source, targets, options, &history, &deviation);
        FK_CHECK(result.isSheet() && result.faces().size() == 1 && checkBody(result, {true, true}).empty());
        if (result.faces().size() != 1) continue;
        checkFaceOrigins(source, result, history);
        const auto &image = *result.face(result.faces().front()).surface;
        // Native U=0 is three quarters of the first chart's U interval.
        // The rational warp moves the physical anchor by 3/10 of leftWarp.
        const Vec3 anchor(.3 * leftWarp, 0., 0.);
        const auto &primary = *left.face(left.faces().front()).surface;
        FK_CHECK(distance(primary.point(0., 0.), anchor) < 1e-14);
        Vec3 jet[4];
        primary.evaluate(options.anchor.x(), options.anchor.y(), 1, jet);
        const auto z = primary.normal(options.anchor.x(), options.anchor.y());
        const auto x = normalized(jet[2]);
        const auto chart = Frame3::fromAxes(jet[0], x, cross(z, x), z);
        const auto prove = [&](const std::vector<SurfaceTargetFace> &patches) {
            return detail::projectedMappingResidualBound(source, sourceFace, patches, options, chart,
                image, {-1., 1.}, {-1., 1.}, budget);
        };
        const auto proof = prove(targets);
        FK_CHECK(proof.status == detail::SurfaceMappingBoundStatus::Certified && proof.upperBound <= budget);
        FK_CHECK(proof.upperBound <= deviation);
        for (double x : {-.75, -.25, .25, .75}) for (double y : {-.75, 0., .5})
            FK_CHECK(distance(image.point(x, y), Vec3(x + anchor.x(), y, y * y / 512.)) <= deviation + 1e-12);

        // Even a height mismatch below the fit cap cannot prove exact overlap.
        auto conflict = panel(-1., 3., true, true, rightWarp, 0x1p-40);
        auto conflicting = targets;
        conflicting.back() = {&conflict, conflict.faces().front()};
        FK_CHECK(prove(conflicting).status == detail::SurfaceMappingBoundStatus::Unsupported);
    }
}

FK_TEST(MappingGlobalCubicCoincidentProjectedOverlap) {
    const auto panel = [](double lo, double hi, double warp, bool split, double heightShift) {
        const std::vector<double> ku{lo, lo, lo, hi, hi, hi};
        const std::vector<double> kv = split ? std::vector<double>{-2., -2., -2., 0., 2., 2., 2.}
                                               : std::vector<double>{-2., -2., -2., 2., 2., 2.};
        const int nv = split ? 4 : 3;
        std::vector<Vec3> poles;
        std::vector<double> weights;
        const std::array<double, 3> xs{lo, lo + (hi - lo) / 4. + warp, hi};
        for (std::size_t i = 0; i < xs.size(); ++i) for (int j = 0; j < nv; ++j) {
            const double x = xs[i], a = kv[j + 1], b = kv[j + 2];
            // U weights depend only on U, so this tensor product represents
            // z = x y² / 512 even though x has a nonlinear rational chart.
            poles.emplace_back(x, .5 * (a + b), x * a * b / 512. + heightShift);
            weights.push_back(1. + i);
        }
        return rectangularSplinePatch(std::make_shared<BSplineSurface>(2, 2, ku, kv, 3, nv, poles, weights));
    };
    const auto source = makePlaneSheet(Frame3(), 1.);
    const auto left = panel(-3., 1., .0625, false, 0.), right = panel(-1., 3., .125, true, 0.);
    FK_CHECK(checkBody(left, {true, true}).empty() && checkBody(right, {true, true}).empty());
    const std::vector<SurfaceTargetFace> targets{{&left, left.faces().front()}, {&right, right.faces().front()}};
    const auto options = wrapOptions();
    const double budget = .2 * std::min(options.tolerance, linearResolution());
    double deviation = -1.;
    const auto result = wrapBodyOnFaces(source, targets, options, nullptr, &deviation);
    FK_CHECK(result.isSheet() && result.faces().size() == 1 && checkBody(result, {true, true}).empty());
    if (result.faces().size() != 1) return;
    const auto &image = *result.face(result.faces().front()).surface;
    const auto &primary = *left.face(left.faces().front()).surface;
    Vec3 jet[4];
    primary.evaluate(0., 0., 1, jet);
    const Vec3 normal = primary.normal(0., 0.), alongU = normalized(jet[2]);
    const auto chart = Frame3::fromAxes(jet[0], alongU, cross(normal, alongU), normal);
    const auto prove = [&](const std::vector<SurfaceTargetFace> &patches) {
        return detail::projectedMappingResidualBound(source, source.faces().front(), patches, options, chart,
            image, {-1., 1.}, {-1., 1.}, budget);
    };
    const auto proof = prove(targets);
    FK_CHECK(proof.status == detail::SurfaceMappingBoundStatus::Certified && proof.upperBound <= budget);
    FK_CHECK(proof.upperBound <= deviation);
    // The independently evaluated warp moves the anchor by (.3 * .0625).
    for (double u : {-.75, 0., .5}) for (double v : {-.75, 0., .5}) {
        const double x = u + .3 * .0625;
        FK_CHECK(distance(image.point(u, v), Vec3(x, v, x * v * v / 512.)) <= deviation + 1e-12);
    }
    const auto conflict = panel(-1., 3., .125, true, 0x1p-40);
    const std::vector<SurfaceTargetFace> conflicting{{&left, left.faces().front()}, {&conflict, conflict.faces().front()}};
    FK_CHECK(prove(conflicting).status == detail::SurfaceMappingBoundStatus::Unsupported);
}

FK_TEST(MappingGlobalQuarticCoincidentProjectedOverlap) {
    const auto panel = [](double lo, double hi, double q, bool split, double peak) {
        const std::vector<double> ku{lo, lo, lo, hi, hi, hi};
        const std::vector<double> kv = split ? std::vector<double>{-2., -2., -2., 0., 2., 2., 2.}
                                               : std::vector<double>{-2., -2., -2., 2., 2., 2.};
        const int nv = split ? 4 : 3;
        const std::array<double, 3> x{lo, .5 * (lo + hi), hi}, xSquared{lo * lo, lo * hi, hi * hi};
        const std::array<double, 3> w{1., q, q * q};
        std::vector<Vec3> poles;
        std::vector<double> weights;
        for (std::size_t i = 0; i < x.size(); ++i) for (int j = 0; j < nv; ++j) {
            const double a = kv[j + 1], b = kv[j + 2];
            // W is the square of the linear weight with poles (1,q).
            // The two U numerators represent x and x² under the same
            // fractional-linear change of coordinate.
            const double perturbation = j == nv - 1 ? peak : 0.;
            poles.emplace_back(x[i], .5 * (a + b), xSquared[i] * a * b / 512. + perturbation);
            weights.push_back(w[i]);
        }
        return rectangularSplinePatch(std::make_shared<BSplineSurface>(2, 2, ku, kv, 3, nv, poles, weights));
    };
    const auto source = makePlaneSheet(Frame3(), 1.);
    const auto left = panel(-3., 1., 2., false, 0.), right = panel(-.5, 3.5, .5, true, 0.);
    FK_CHECK(checkBody(left, {true, true}).empty() && checkBody(right, {true, true}).empty());
    const std::vector<SurfaceTargetFace> targets{{&left, left.faces().front()}, {&right, right.faces().front()}};
    const auto options = wrapOptions();
    const double budget = .2 * std::min(options.tolerance, linearResolution());
    double deviation = -1.;
    const auto result = wrapBodyOnFaces(source, targets, options, nullptr, &deviation);
    FK_CHECK(result.isSheet() && result.faces().size() == 1 && checkBody(result, {true, true}).empty());
    if (result.faces().size() != 1) return;
    const auto &image = *result.face(result.faces().front()).surface;
    const auto &primary = *left.face(left.faces().front()).surface;
    Vec3 jet[4];
    primary.evaluate(0., 0., 1, jet);
    const Vec3 normal = primary.normal(0., 0.), alongU = normalized(jet[2]);
    const auto chart = Frame3::fromAxes(jet[0], alongU, cross(normal, alongU), normal);
    const auto prove = [&](const std::vector<SurfaceTargetFace> &patches) {
        return detail::projectedMappingResidualBound(source, source.faces().front(), patches, options, chart,
            image, {-1., 1.}, {-1., 1.}, budget);
    };
    const auto proof = prove(targets);
    FK_CHECK(proof.status == detail::SurfaceMappingBoundStatus::Certified && proof.upperBound <= budget);
    FK_CHECK(proof.upperBound <= deviation);
    // The left fractional-linear map takes normalized native 3/4 to
    // 6/7, putting the physical anchor at -3 + 4·6/7 = 3/7.
    for (double u : {-.99, 0., .75}) for (double v : {-.75, 0., .5}) {
        const double x = u + 3. / 7.;
        FK_CHECK(distance(image.point(u, v), Vec3(x, v, x * x * v * v / 512.)) <= deviation + 1e-12);
    }
    // V's lower span is untouched, so the negative requires checking the
    // later span even when the inferred common quartic is exact.
    const auto conflict = panel(-.5, 3.5, .5, true, 0x1p-40);
    const std::vector<SurfaceTargetFace> conflicting{{&left, left.faces().front()}, {&conflict, conflict.faces().front()}};
    FK_CHECK(prove(conflicting).status == detail::SurfaceMappingBoundStatus::Unsupported);
}

namespace {
constexpr double quinticShear = .125, quinticHeightScale = 1. / 262144.;

Body quinticOverlapPanel(double lo, double hi, double uScale, bool split, double peak = 0.) {
    const std::vector<double> ku{lo, lo, lo, hi, hi, hi};
    const std::vector<double> kv = split
        ? std::vector<double>{-2., -2., -2., 0., 2., 2., 2.}
        : std::vector<double>{-2., -2., -2., 2., 2., 2.};
    const int nv = split ? 4 : 3;
    std::vector<Vec3> poles;
    for (int i = 0; i < 3; ++i) for (int j = 0; j < nv; ++j) {
        const double a = uScale * ku[i + 1], b = uScale * ku[i + 2];
        const double c = kv[j + 1], d = kv[j + 2], v = .5 * (c + d);
        // Native height is (uScale*u)² v. Undoing the projected
        // quadratic shear yields the physical quintic (x-a*y²)² y.
        poles.emplace_back(.5 * (a + b) + quinticShear * c * d, v,
                           quinticHeightScale * a * b * v + (j == nv - 1 ? peak : 0.));
    }
    return rectangularSplinePatch(std::make_shared<BSplineSurface>(2, 2, ku, kv, 3, nv, poles));
}
}

FK_TEST(MappingGlobalQuinticCoincidentProjectedOverlap) {
    const auto source = makePlaneSheet(Frame3(), 1.);
    const auto left = quinticOverlapPanel(-2., 2., 1.5, false, 0.), right = quinticOverlapPanel(-.5, 3.5, 1., true, 0.);
    FK_CHECK(checkBody(left, {true, true}).empty() && checkBody(right, {true, true}).empty());
    const std::vector<SurfaceTargetFace> targets{{&left, left.faces().front()}, {&right, right.faces().front()}};
    const auto options = wrapOptions();
    const double budget = .2 * std::min(options.tolerance, linearResolution());
    double deviation = -1.;
    const auto result = wrapBodyOnFaces(source, targets, options, nullptr, &deviation);
    FK_CHECK(result.isSheet() && result.faces().size() == 1 && checkBody(result, {true, true}).empty());
    if (result.faces().size() != 1) return;
    const auto &image = *result.face(result.faces().front()).surface;
    const auto prove = [&](const std::vector<SurfaceTargetFace> &patches) {
        return detail::projectedMappingResidualBound(source, source.faces().front(), patches, options, Frame3(),
            image, {-1., 1.}, {-1., 1.}, budget);
    };
    const auto proof = prove(targets);
    FK_CHECK(proof.status == detail::SurfaceMappingBoundStatus::Certified && proof.upperBound <= budget);
    FK_CHECK(proof.upperBound <= deviation);
    for (double u : {-.75, 0., .75}) for (double v : {-.75, 0., .75}) {
        const double native = u - quinticShear * v * v;
        FK_CHECK(distance(image.point(u, v), Vec3(u, v, quinticHeightScale * native * native * v)) <= deviation + 1e-12);
    }
    const auto conflict = quinticOverlapPanel(-.5, 3.5, 1., true, 0x1p-40);
    const std::vector<SurfaceTargetFace> conflicting{{&left, left.faces().front()}, {&conflict, conflict.faces().front()}};
    FK_CHECK(prove(conflicting).status == detail::SurfaceMappingBoundStatus::Unsupported);
}

FK_TEST(MappingGlobalRationalHeightCoincidentProjectedOverlap) {
    constexpr double heightScale = 3. / 8388608.;
    const auto panel = [](double lo, double hi, double warp, bool split, double peak) {
        const std::vector<double> ku = split ? std::vector<double>{lo, lo, lo, 0., hi, hi, hi}
                                              : std::vector<double>{lo, lo, lo, hi, hi, hi};
        const std::vector<double> kv{-1.5, -1.5, -1.5, 1.5, 1.5, 1.5};
        const int nu = split ? 4 : 3;
        const std::array<double, 3> w{1., 1.5, 2.}, y{-1.5, -.5, 1.5};
        std::vector<Vec3> poles;
        std::vector<double> weights;
        for (int i = 0; i < nu; ++i) {
            const double a = (ku[i + 1] - lo) / (hi - lo), b = (ku[i + 2] - lo) / (hi - lo);
            const double mean = .5 * (a + b);
            const double x = lo + (hi - lo) * mean + 2. * warp * (mean - a * b);
            for (std::size_t j = 0; j < y.size(); ++j) {
                // Homogeneous V coordinates represent y, y² and the
                // positive affine weight 1 + (y + 1.5) / 3.
                const double ySquared = kv[j + 1] * kv[j + 2];
                const double perturbation = i == nu - 1 ? peak : 0.;
                poles.emplace_back(x, y[j], (heightScale * ySquared) / w[j] + perturbation);
                weights.push_back(w[j]);
            }
        }
        return rectangularSplinePatch(std::make_shared<BSplineSurface>(2, 2, ku, kv, nu, 3, poles, weights));
    };
    const auto source = makePlaneSheet(Frame3(), 1.);
    const auto left = panel(-3., 1., .0625, false, 0.), right = panel(-1., 3., .125, true, 0.);
    FK_CHECK(checkBody(left, {true, true}).empty() && checkBody(right, {true, true}).empty());
    const std::vector<SurfaceTargetFace> targets{{&left, left.faces().front()}, {&right, right.faces().front()}};
    const auto options = wrapOptions();
    const double budget = .2 * std::min(options.tolerance, linearResolution());
    double deviation = -1.;
    const auto result = wrapBodyOnFaces(source, targets, options, nullptr, &deviation);
    FK_CHECK(result.isSheet() && result.faces().size() == 1 && checkBody(result, {true, true}).empty());
    if (result.faces().size() != 1) return;
    const auto &image = *result.face(result.faces().front()).surface;
    const auto &primary = *left.face(left.faces().front()).surface;
    Vec3 jet[4];
    primary.evaluate(0., 0., 1, jet);
    const Vec3 normal = primary.normal(0., 0.), alongU = normalized(jet[2]);
    const auto chart = Frame3::fromAxes(jet[0], alongU, cross(normal, alongU), normal);
    const auto prove = [&](const std::vector<SurfaceTargetFace> &patches) {
        return detail::projectedMappingResidualBound(source, source.faces().front(), patches, options, chart,
            image, {-1., 1.}, {-1., 1.}, budget);
    };
    const auto proof = prove(targets);
    FK_CHECK(proof.status == detail::SurfaceMappingBoundStatus::Certified && proof.upperBound <= budget);
    FK_CHECK(proof.upperBound <= deviation);
    // At native U=0, the left warp displaces the physical X by 3/128.
    for (double u : {-.75, 0., .75}) for (double v : {-.75, 0., .5}) {
        const double height = heightScale * v * v / (1. + (v + 1.5) / 3.);
        FK_CHECK(distance(image.point(u, v), Vec3(u + 3. / 128., v, height)) <= deviation + 1e-12);
    }
    const auto conflict = panel(-1., 3., .125, true, 0x1p-40);
    const std::vector<SurfaceTargetFace> conflicting{{&left, left.faces().front()}, {&conflict, conflict.faces().front()}};
    // The perturbed pole is inactive on the first U span used to infer the model.
    FK_CHECK(prove(conflicting).status == detail::SurfaceMappingBoundStatus::Unsupported);
}

namespace {
void checkRationalDenominatorCoincidentProjectedOverlap(bool cubicNumerator) {
    constexpr double yRadius = 105. / 128.;
    constexpr double heightScale = 21. / 67108864.;
    constexpr double radius = .625;
    const auto panel = [cubicNumerator](double xRadius, double q, bool split, double peak) {
        const std::vector<double> ku{-xRadius, -xRadius, -xRadius, xRadius, xRadius, xRadius};
        const std::vector<double> kv = split
            ? std::vector<double>{-yRadius, -yRadius, -yRadius, 0., yRadius, yRadius, yRadius}
            : std::vector<double>{-yRadius, -yRadius, -yRadius, yRadius, yRadius, yRadius};
        const int nv = split ? 4 : 3;
        const std::array<double, 3> x{-xRadius, -xRadius * xRadius / 4., xRadius};
        const std::array<double, 3> dx{4. - xRadius, 4., 4. + xRadius};
        const std::array<double, 3> wx{dx[0], q * dx[1], q * q * dx[2]};
        std::vector<Vec3> poles;
        std::vector<double> weights;
        for (std::size_t i = 0; i < x.size(); ++i) for (int j = 0; j < nv; ++j) {
            const double a = kv[j + 1] / yRadius, b = kv[j + 2] / yRadius;
            const double da = 1.5 + .5 * a, db = 1.5 + .5 * b;
            const double wy = .5 * (da + db);
            const double y = yRadius * (a * db + b * da) / (2. * wy);
            // The product weights represent the two distinct factors in
            // the physical denominator, and q changes the U projection.
            // For the cubic numerator Xh has the cross coefficient
            // of X times the degree-elevated linear Möbius weight.
            const double numeratorX = cubicNumerator ? (i == 1 ? 0. : x[i]) : 1.;
            const double z = (heightScale / dx[i]) * numeratorX * (a * b) / wy;
            poles.emplace_back(x[i], y, z + (j == nv - 1 ? peak : 0.));
            weights.push_back(wx[i] * wy);
        }
        return rectangularSplinePatch(std::make_shared<BSplineSurface>(2, 2, ku, kv, 3, nv, poles, weights));
    };
    const auto source = makePlaneSheet(Frame3(), radius);
    const auto left = panel(3., .5, false, 0.), right = panel(2., .5, true, 0.);
    FK_CHECK(checkBody(left, {true, true}).empty() && checkBody(right, {true, true}).empty());
    const std::vector<SurfaceTargetFace> targets{{&left, left.faces().front()}, {&right, right.faces().front()}};
    const auto options = wrapOptions();
    const double budget = .2 * std::min(options.tolerance, linearResolution());
    double deviation = -1.;
    const auto result = wrapBodyOnFaces(source, targets, options, nullptr, &deviation);
    FK_CHECK(result.isSheet() && result.faces().size() == 1 && checkBody(result, {true, true}).empty());
    if (result.faces().size() != 1) return;
    const auto &image = *result.face(result.faces().front()).surface;
    const auto &primary = *left.face(left.faces().front()).surface;
    Vec3 jet[4];
    primary.evaluate(0., 0., 1, jet);
    const Vec3 normal = primary.normal(0., 0.), alongU = normalized(jet[2]);
    const auto chart = Frame3::fromAxes(jet[0], alongU, cross(normal, alongU), normal);
    const auto prove = [&](const std::vector<SurfaceTargetFace> &patches) {
        return detail::projectedMappingResidualBound(source, source.faces().front(), patches, options, chart,
            image, {-radius, radius}, {-radius, radius}, budget);
    };
    const auto proof = prove(targets);
    FK_CHECK(proof.status == detail::SurfaceMappingBoundStatus::Certified && proof.upperBound <= budget);
    FK_CHECK(proof.upperBound <= deviation);
    // Normalized native U=1/2 maps to 1/3 on the left. The analytic
    // anchor is therefore x=-1 and the denominator has a nonzero XY term.
    for (double u : {-.5, 0., .5}) for (double v : {-.5, 0., .5}) {
        const double x = u - 1., y = v / yRadius;
        const double height = heightScale * (cubicNumerator ? x : 1.) * y * y /
            ((x + 4.) * (1.5 + .5 * y));
        FK_CHECK(distance(image.point(u, v), Vec3(x, v, height)) <= deviation + 1e-12);
    }
    const auto conflict = panel(2., .5, true, 0x1p-40);
    const std::vector<SurfaceTargetFace> conflicting{{&left, left.faces().front()}, {&conflict, conflict.faces().front()}};
    // Only V's second span differs, so a valid candidate on the first is
    // insufficient to certify these two charts.
    FK_CHECK(prove(conflicting).status == detail::SurfaceMappingBoundStatus::Unsupported);
}

}

FK_TEST(MappingGlobalQuadraticDenominatorCoincidentProjectedOverlap) {
    checkRationalDenominatorCoincidentProjectedOverlap(false);
}

FK_TEST(MappingGlobalCubicNumeratorCoincidentProjectedOverlap) {
    checkRationalDenominatorCoincidentProjectedOverlap(true);
}

FK_TEST(MappingGlobalRationalStripBoundaryOrder) {
    const auto panel = [](double first, double last, bool rational) {
        const std::vector<double> ku{first, first, last, last}, kv{-2., -2., -2., 2., 2., 2.};
        std::vector<Vec3> poles;
        std::vector<double> weights;
        for (int i = 0; i < 2; ++i) for (int j = 0; j < 3; ++j) {
            const double a = kv[j + 1], b = kv[j + 2], v2 = a * b;
            poles.emplace_back((i ? last : first) + v2 / 16., .5 * (a + b), v2 / 512.);
            if (rational) weights.push_back(1. + i);
        }
        return rectangularSplinePatch(std::make_shared<BSplineSurface>(1, 2, ku, kv, 2, 3, poles, weights));
    };
    const auto left = panel(-2., 0., false), right = panel(0., 2., true);
    const auto source = makePlaneSheet(Frame3(), 1.);
    const auto options = wrapOptions();
    const double budget = .2 * std::min(options.tolerance, linearResolution());
    const std::vector<SurfaceTargetFace> targets{{&left, left.faces().front()}, {&right, right.faces().front()}};
    double deviation = -1.;
    const auto result = wrapBodyOnFaces(source, targets, options, nullptr, &deviation);
    FK_CHECK(result.isSheet() && result.faces().size() == 1 && checkBody(result, {true, true}).empty());
    if (result.faces().size() != 1) return;
    const auto &image = *result.face(result.faces().front()).surface;
    const auto proof = detail::projectedMappingResidualBound(source, source.faces().front(), targets, options,
        Frame3(), image, {-1., 1.}, {-1., 1.}, budget);
    FK_CHECK(proof.status == detail::SurfaceMappingBoundStatus::Certified && proof.upperBound <= budget);
    for (double y : {-.75, -.25, .5}) {
        const double seam = y * y / 16.;
        FK_CHECK(distance(image.point(seam, y), Vec3(seam, y, y * y / 512.)) <= deviation + 1e-12);
    }
}

FK_TEST(MappingGlobalRationalGraphsWithVariableHeight) {
    constexpr double tilt = 1e-4, relief = 4e-9;
    const Frame3 frame({}, Vec3(-tilt, 0., 1.), Vec3(1., 0., tilt));
    const auto source = exactRectangularSource(frame);
    const auto left = rationalGraphPanel(-2., 0., 0., false);
    auto options = wrapOptions();
    const double budget = .2 * std::min(options.tolerance, linearResolution());
    const std::vector<double> k{-1., -1., -1., -1., 1., 1., 1., 1.};
    for (double warp : {0., .125}) for (double shift : {-.0002, 0., .0002}) {
        const auto right = rationalGraphPanel(0., 2., relief, true, warp);
        const auto &graph = *right.face(right.faces().front()).surface;
        const std::vector<SurfaceTargetFace> targets{{&left, left.faces().front()}, {&right, right.faces().front()}};
        options.normalOffset = shift;
        BodyHistory history;
        double deviation = -1.;
        const auto result = wrapBodyOnFaces(source, targets, options, &history, &deviation);
        FK_CHECK(result.isSheet() && result.faces().size() == 1 && checkBody(result, {true, true}).empty());
        checkFaceOrigins(source, result, history);
        const auto &image = *result.face(result.faces().front()).surface;
        const auto proof = detail::projectedMappingResidualBound(source, source.faces().front(), targets,
            options, Frame3(), image, {-1., 1.}, {-1., 1.}, budget);
        FK_CHECK(proof.status == detail::SurfaceMappingBoundStatus::Certified && proof.upperBound <= budget);
        for (double nativeU : {-.8, .2, .875}) {
            const auto contact = nativeU < 0. ? Vec3(nativeU, -.25, 0.) : graph.point(nativeU, -.25);
            const double u = contact.x() / frame.xDir().x(), h = shift + u * frame.xDir().z();
            const auto normal = nativeU < 0. ? Vec3(0., 0., 1.) : graph.normal(nativeU, -.25);
            const auto expected = contact + h * normal;
            FK_CHECK(distance(image.point(u, -.25), expected) <= deviation + 1e-12);
        }
        std::vector<Vec3> poles;
        for (double u : {-1., -1./3., 1./3., 1.}) for (double v : {-1., -1./3., 1./3., 1.})
            poles.emplace_back(1. + u * frame.xDir().x(), v, u * frame.xDir().z() + shift + 1e-7);
        const BSplineSurface wrong(3, 3, k, k, 4, 4, poles);
        auto onlyRight = options;
        onlyRight.shift = {1., 0.}; // Every source point projects onto the rational patch.
        const auto rejected = detail::projectedMappingResidualBound(source, source.faces().front(), targets,
            onlyRight, Frame3(), wrong, {-1., 1.}, {-1., 1.}, budget);
        FK_CHECK(rejected.status == detail::SurfaceMappingBoundStatus::Failed);
    }
}

FK_TEST(MappingGlobalNonlinearGraphWithBiaxialWeights) {
    const auto left = rationalGraphPanel(-2., 0., 0., false);
    const std::array<double, 3> modulation{{.125, .25, .0625}};
    const std::array<double, 3> xPoles{{0., .625, 2.}};
    const std::vector<double> uKnots{0., 0., 0., 2., 2., 2.}, vKnots{-2., -2., -2., 2., 2., 2.};
    const std::vector<double> imageKnots{-1., -1., -1., -1., 1., 1., 1., 1.};
    constexpr double nativeU = .875, nativeV = -.25;
    constexpr double t = nativeU / 2.;
    const std::array<double, 3> b{{(1.-t)*(1.-t), 2.*t*(1.-t), t*t}};
    constexpr double tv = (nativeV + 2.) / 4.;
    constexpr double middleV = 2. * tv * (1. - tv);
    const std::array<Vec2, 3> warps{{{0., 0.}, {.125, 0.}, {.625, .0625}}};
    for (const auto &warp : warps) for (double tilt : {0., 1e-4}) {
        const double yWarp = warp.x(), footprintWarp = warp.y();
        const Frame3 frame({}, Vec3(-tilt, 0., 1.), Vec3(1., 0., tilt));
        const auto source = exactRectangularSource(frame);
        const auto sourceFace = source.faces().front();
        auto options = wrapOptions();
        options.shift = {.125, 0.};
        const double budget = .2 * std::min(options.tolerance, linearResolution());
        std::vector<Vec3> candidatePoles;
        for (double u : {-1., -1./3., 1./3., 1.}) for (double v : {-1., -1./3., 1./3., 1.})
            candidatePoles.emplace_back(frame.xDir().x() * u + options.shift.x(), v, frame.xDir().z() * u);
        const BSplineSurface candidate(3, 3, imageKnots, imageKnots, 4, 4, candidatePoles);
        for (double relief : {4e-9, 2e-7}) {
            std::vector<Vec3> poles;
            std::vector<double> weights;
            for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) {
                weights.push_back((1. + i) * (1. + (j - 1) * modulation[i]));
                const double y = j == 1 ? -2. * modulation[i] + yWarp * (i - 1) :
                    2. * (j - 1) + (j == 2 ? .5 * i * footprintWarp : 0.);
                const double x = xPoles[i] + (i == 2 && j == 1 ? footprintWarp : 0.);
                poles.emplace_back(x, y, i == 2 ? relief : 0.);
            }
            constexpr int row = 3;
            FK_CHECK(weights[0] * weights[row + 1] != weights[1] * weights[row]);
            FK_CHECK(weights[0] != weights[1] && weights[0] != weights[row]);
            const auto graph = std::make_shared<BSplineSurface>(2, 2, uKnots, vKnots, 3, 3, poles, weights);
            const auto right = rectangularSplinePatch(graph);
            FK_CHECK(checkBody(right, {true, true}).empty());
            const std::vector<SurfaceTargetFace> targets{{&left, left.faces().front()}, {&right, right.faces().front()}};
            double numerator = 0., denominator = 0., height = 0., transverse = 0.;
            for (int i = 0; i < 3; ++i) {
                const double w = b[i] * (1. + i) * (1. + .5 * nativeV * modulation[i]);
                numerator += w * xPoles[i];
                denominator += w;
                transverse += b[i] * (1. + i) * (yWarp * (i - 1) * middleV +
                    tv * tv * (1. + modulation[i]) * .5 * i * footprintWarp);
                if (i == 2) {
                    numerator += b[i] * (1. + i) * footprintWarp * middleV;
                    height = relief * w;
                }
            }
            const Vec3 contact(numerator / denominator, nativeV + transverse / denominator, height / denominator);
            if (yWarp != 0.) FK_CHECK(std::abs(contact.y() - nativeV) > 1e-4);
            FK_CHECK(distance(graph->point(nativeU, nativeV), contact) < 1e-14);
            const double u = (contact.x() - options.shift.x()) / frame.xDir().x();
            const Vec3 expected = contact + (u * frame.xDir().z()) * graph->normal(nativeU, nativeV);
            const double residual = distance(candidate.point(u, contact.y()), expected);
            const auto proof = detail::projectedMappingResidualBound(source, sourceFace, targets,
                options, Frame3(), candidate, {-1., 1.}, {-1., 1.}, budget);
            if (relief > 1e-8) {
                FK_CHECK(residual > budget && proof.status == detail::SurfaceMappingBoundStatus::Failed);
                continue;
            }
            FK_CHECK(proof.status == detail::SurfaceMappingBoundStatus::Certified &&
                     residual <= proof.upperBound + 1e-12 && proof.upperBound <= budget);
            BodyHistory history;
            double deviation = -1.;
            const auto result = wrapBodyOnFaces(source, targets, options, &history, &deviation);
            FK_CHECK(result.faces().size() == 1 && result.isSheet() && checkBody(result, {true, true}).empty());
            checkFaceOrigins(source, result, history);
            const auto &image = *result.face(result.faces().front()).surface;
            FK_CHECK(distance(image.point(u, contact.y()), expected) <= deviation + 1e-12);
        }
    }
}

FK_TEST(MappingGlobalRationalGraphAcrossKnots) {
    const auto source = makePlaneSheet(Frame3(), 1.);
    const auto left = rationalGraphPanel(-2., 0., 0., false);
    auto options = wrapOptions();
    options.shift = {.75, 0.};
    const double budget = .2 * std::min(options.tolerance, linearResolution());
    const std::vector<double> ku{0., 0., 0., 1., 2., 2., 2.};
    const std::vector<double> imageKnots{-1., -1., -1., -1., 1., 1., 1., 1.};
    std::vector<Vec3> imagePoles;
    for (double u : {-1., -1./3., 1./3., 1.}) for (double v : {-1., -1./3., 1./3., 1.})
        imagePoles.emplace_back(u + options.shift.x(), v, 0.);
    const BSplineSurface flat(3, 3, imageKnots, imageKnots, 4, 4, imagePoles);
    for (bool splitV : {false, true}) for (double relief : {4e-9, 6e-7}) {
        const auto kv = splitV ? std::vector<double>{-2., -2., 0., 2., 2.}
                              : std::vector<double>{-2., -2., 2., 2.};
        const std::array<double, 4> x{{0., .45, 1.3, 2.}}, w{{1., 1.25, 1.5, 2.}};
        std::vector<Vec3> poles;
        std::vector<double> weights;
        for (int i = 0; i < 4; ++i) for (std::size_t j = 1; j + 1 < kv.size(); ++j) {
            poles.emplace_back(x[i], kv[j], i == 3 ? relief : 0.);
            weights.push_back(w[i]);
        }
        const auto surface = std::make_shared<BSplineSurface>(2, 1, ku, kv, 4, int(kv.size()) - 2,
                                                             poles, weights);
        const auto right = rectangularSplinePatch(surface);
        const std::vector<SurfaceTargetFace> targets{{&left, left.faces().front()}, {&right, right.faces().front()}};
        const auto proof = detail::projectedMappingResidualBound(source, source.faces().front(), targets,
            options, Frame3(), flat, {-1., 1.}, {-1., 1.}, budget);
        if (relief > budget) {
            const auto point = surface->point(1.5, .25);
            FK_CHECK(std::abs(point.x() - options.shift.x()) < 1. && point.z() > budget);
            FK_CHECK(proof.status == detail::SurfaceMappingBoundStatus::Failed);
            continue;
        }
        FK_CHECK(proof.status == detail::SurfaceMappingBoundStatus::Certified && proof.upperBound <= budget);
        BodyHistory history;
        double deviation = -1.;
        const auto result = wrapBodyOnFaces(source, targets, options, &history, &deviation);
        FK_CHECK(result.isSheet() && result.faces().size() == 1 && checkBody(result, {true, true}).empty());
        if (result.faces().size() != 1) continue;
        checkFaceOrigins(source, result, history);
        const auto &image = *result.face(result.faces().front()).surface;
        const auto fit = detail::projectedMappingResidualBound(source, source.faces().front(), targets,
            options, Frame3(), image, {-1., 1.}, {-1., 1.}, budget);
        FK_CHECK(fit.status == detail::SurfaceMappingBoundStatus::Certified && fit.upperBound <= deviation);
        for (double u : {.75, 1., 1.25}) for (double v : {-.5, 0., .5}) {
            const auto expected = surface->point(u, v);
            FK_CHECK(distance(expected, image.point(expected.x() - options.shift.x(), expected.y())) <= deviation + 1e-12);
        }
    }
}

FK_TEST(MappingGlobalNonlinearProjectionWithVariableHeight) {
    constexpr double tilt = 1e-4;
    const Frame3 frame({}, Vec3(-tilt, 0., 1.), Vec3(1., 0., tilt));
    const auto source = exactRectangularSource(frame);
    const auto sourceBefore = writeBodyBinary(source);
    auto options = wrapOptions();
    options.angle = .4;
    options.shift = {.05, -.08};
    const double c = std::cos(options.angle), s = std::sin(options.angle);
    const auto sourceAt = [&](const Vec3 &point) {
        const auto xy = Vec2(point.x(), point.y()) - options.shift;
        return Vec2((c * xy.x() + s * xy.y()) / frame.xDir().x(), -s * xy.x() + c * xy.y());
    };
    const double budget = .2 * std::min(options.tolerance, linearResolution());
    struct Trial { bool threePatches; double offset, bump; };
    const std::array<Trial, 5> trials{{{false, 0., 0.}, {false, 0., 2.5e-8}, {true, 0., 2.5e-8},
                                      {true, .0002, 0.}, {true, -.0002, 0.}}};
    for (const auto &trial : trials) {
        options.normalOffset = trial.offset;
        const auto quilt = nonlinearProjectionQuilt(trial.bump, true, trial.threePatches);
        const auto targetBefore = writeBodyBinary(quilt);
        const auto faces = quilt.faces();
        std::vector<SurfaceTargetFace> targets;
        for (auto face : faces) targets.push_back({&quilt, face});
        if (faces.size() == 3) std::swap(targets[0], targets[1]);
        const auto &patch = *quilt.face(faces.back()).surface;
        auto history = sameIndexHistory(source, source);
        const auto historyBefore = historyStamp(history);
        double deviation = -123.;
        bool rejected = false;
        try {
            const auto result = wrapBodyOnFaces(source, targets, options, &history, &deviation);
            FK_CHECK(result.isSheet() && result.faces().size() == 1);
            FK_CHECK(checkBody(result, {true, true}).empty());
            const auto &image = *result.face(result.faces().front()).surface;
            const auto proof = detail::projectedMappingResidualBound(source, source.faces().front(), targets,
                options, Frame3(), image, {-1., 1.}, {-1., 1.}, budget);
            FK_CHECK(proof.status == detail::SurfaceMappingBoundStatus::Certified && proof.upperBound <= budget);
            const auto check = [&](const Surface &surface, double u, double v) {
                const auto hit = surface.point(u, v);
                const auto uv = sourceAt(hit);
                const double h = trial.offset + frame.xDir().z() * uv.x();
                const auto expected = hit + h * surface.normal(u, v);
                FK_CHECK(distance(image.point(uv.x(), uv.y()), expected) <= deviation + 1e-12);
                return h;
            };
            const double first = check(*quilt.face(faces.front()).surface, -.8, -.2);
            const double last = check(patch, .6, .2);
            FK_CHECK((first < 0.) == (trial.offset <= 0.));
            FK_CHECK((last > 0.) == (trial.offset >= 0.));
            constexpr double t = 17. / 73.;
            check(patch, (1240. + 16. * t) / 4096., (680. + 16. * t) / 4096.);
            checkFaceOrigins(source, result, history);
        } catch (const std::domain_error &error) {
            rejected = true;
            FK_CHECK(std::string(error.what()).find("residuo globale del carrier") != std::string::npos);
        }
        FK_CHECK(rejected == (trial.bump != 0.));
        if (rejected) FK_CHECK(deviation == -123. && historyStamp(history) == historyBefore);
        FK_CHECK(writeBodyBinary(source) == sourceBefore && writeBodyBinary(quilt) == targetBefore);
    }
}

FK_TEST(MappingGlobalNonlinearProjectionReliefOutsideSourceMaterial) {
    auto options = wrapOptions();
    options.angle = .4;
    options.shift = {.05, -.08};
    // A circular source cutout contains the entire projected support of the
    // quadratic relief, including the interior peak used by the negative case.
    const Vec2 projection(.3 + .3 * .3 / 1024., .165);
    const Vec2 shifted = projection - options.shift;
    const double c = std::cos(options.angle), s = std::sin(options.angle);
    const Vec2 center(c * shifted.x() + s * shifted.y(), -s * shifted.x() + c * shifted.y());
    constexpr double radius = .03;
    ProfileRegion region;
    const std::array<Vec2, 4> corners{{{-1., -1.}, {1., -1.}, {1., 1.}, {-1., 1.}}};
    for (std::size_t i = 0; i < corners.size(); ++i) {
        const Vec2 edge = corners[(i + 1) % corners.size()] - corners[i];
        region.outer.segments.push_back({std::make_shared<Line<2>>(corners[i], edge), {0., norm(edge)}});
    }
    const ProfileLoop hole{{{std::make_shared<Circle<2>>(makeCircle(center, radius)), {0., kTwoPi}}}};
    region.holes = {hole};
    const auto source = planarSheet(Frame3(), {region});
    const auto sourceFace = source.faces().front();
    FK_CHECK(FaceParameterClassifier(source, sourceFace, 1e-8).classify(center) == PointLocation::Outside);
    const auto quilt = nonlinearProjectionQuilt(2.5e-8);
    const auto faces = quilt.faces();
    const std::vector<SurfaceTargetFace> targets{{&quilt, faces[0]}, {&quilt, faces[1]}};
    BodyHistory history;
    double deviation = -123.;
    const auto result = wrapBodyOnFaces(source, targets, options, &history, &deviation);
    FK_CHECK(result.isSheet() && result.faces().size() == 1);
    FK_CHECK(checkBody(result, {true, true}).empty());
    FK_CHECK(std::isfinite(deviation) && deviation >= 0. && deviation <= options.tolerance);
    checkFaceOrigins(source, result, history);
    if (result.faces().size() == 1)
        FK_CHECK(result.face(result.faces().front()).loops.size() == 2);
}

FK_TEST(MappingGlobalPolynomialSourceGraphWithVariableHeight) {
    const auto graph = [](double lo, double hi, double bottom, double top, bool source) {
        const std::vector<double> u{lo, lo, lo, hi, hi, hi};
        const std::vector<double> v{bottom, bottom, bottom, top, top, top};
        std::vector<Vec3> poles;
        for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) {
            const double x = .5 * (u[i + 1] + u[i + 2]);
            const double y = .5 * (v[j + 1] + v[j + 2]);
            // Source Z = (u²-v²)/8192. The support is tangent across X=0.
            const double z = source ? (u[i + 1] * u[i + 2] - v[j + 1] * v[j + 2]) / 8192.
                                    : u[i + 1] * u[i + 2] / 512.;
            poles.emplace_back(x, y, z);
        }
        return std::make_shared<BSplineSurface>(2, 2, u, v, 3, 3, poles);
    };
    const auto original = graph(-1., 1., -1., 1., true);
    const auto source = rectangularSplinePatch(original);
    const auto sourceBefore = writeBodyBinary(source);
    const auto sourceFace = source.faces().front();
    const auto l = graph(-2., 0., -2., 2., false), r = graph(0., 2., -2., 2., false);
    const auto left = rectangularSplinePatch(l), right = rectangularSplinePatch(r);
    const std::vector<SurfaceTargetFace> targets{{&left, left.faces().front()}, {&right, right.faces().front()}};
    auto options = wrapOptions();
    options.sourcePlane = Frame3({0., 0., 0x1p-16}, {0., 0., 1.}, {1., 0., 0.});
    options.normalOffset = 0x1p-17;
    const double budget = .2 * std::min(options.tolerance, linearResolution());
    BodyHistory history;
    double deviation = -1.;
    const auto result = wrapBodyOnFaces(source, targets, options, &history, &deviation);
    FK_CHECK(result.isSheet() && result.faces().size() == 1 && checkBody(result, {true, true}).empty());
    checkFaceOrigins(source, result, history);
    const auto &image = *result.face(result.faces().front()).surface;
    const auto proof = detail::projectedMappingResidualBound(source, sourceFace, targets, options,
        Frame3(), image, {-1., 1.}, {-1., 1.}, budget);
    FK_CHECK(proof.status == detail::SurfaceMappingBoundStatus::Certified && proof.upperBound <= budget);
    FK_CHECK(deviation >= proof.upperBound && deviation <= options.tolerance);
    for (double u : {-.75, -.25, 0., .25, .75}) for (double v : {-.75, .25, .75}) {
        const auto &support = u < 0. ? *l : *r;
        const double h = (u * u - v * v) / 8192. - 0x1p-17;
        const auto expected = support.point(u, v) + h * support.normal(u, v);
        FK_CHECK(distance(expected, image.point(u, v)) <= proof.upperBound + 1e-12);
    }
    const std::vector<double> k{-1., -1., -1., -1., 1., 1., 1., 1.};
    std::vector<Vec3> poles;
    // Same support parabola as the two targets, with all source relief lost.
    const std::array<double, 4> x{-1., -1. / 3., 1. / 3., 1.};
    const std::array<double, 4> z{1., -1. / 3., -1. / 3., 1.};
    for (std::size_t i = 0; i < x.size(); ++i) for (double y : x)
        poles.emplace_back(x[i], y, z[i] / 512.);
    const BSplineSurface wrong(3, 3, k, k, 4, 4, poles);
    const auto lost = detail::projectedMappingResidualBound(source, sourceFace, targets, options,
        Frame3(), wrong, {-1., 1.}, {-1., 1.}, budget);
    FK_CHECK(lost.status == detail::SurfaceMappingBoundStatus::Failed);
    options.sourcePlane = Frame3({}, {0., .125, 1.}, {1., 0., 0.});
    const auto inclined = wrapBodyOnFaces(source, targets, options, &history, &deviation);
    FK_CHECK(inclined.isSheet() && inclined.faces().size() == 1 && checkBody(inclined, {true, true}).empty());
    if (inclined.faces().size() != 1) return;
    checkFaceOrigins(source, inclined, history);
    const auto &inclinedImage = *inclined.face(inclined.faces().front()).surface;
    const auto tilted = detail::projectedMappingResidualBound(source, sourceFace, targets, options,
        Frame3(), inclinedImage, {-1., 1.}, {-1., 1.}, budget);
    FK_CHECK(tilted.status == detail::SurfaceMappingBoundStatus::Certified && tilted.upperBound <= budget);
    FK_CHECK(deviation >= tilted.upperBound && deviation <= options.tolerance);
    for (double u : {-.75, -.25, .25, .75}) for (double v : {-.75, .25, .75}) {
        const auto &support = u < 0. ? *l : *r;
        const auto local = options.sourcePlane.toLocal(Vec3(u, v, (u * u - v * v) / 8192.));
        const auto expected = support.point(local.x(), local.y())
            + (local.z() + options.normalOffset) * support.normal(local.x(), local.y());
        FK_CHECK(distance(expected, inclinedImage.point(u, v)) <= tilted.upperBound + 1e-12);
    }
    const auto inclinedLost = detail::projectedMappingResidualBound(source, sourceFace, targets, options,
        Frame3(), wrong, {-1., 1.}, {-1., 1.}, budget);
    FK_CHECK(inclinedLost.status == detail::SurfaceMappingBoundStatus::Failed);

    options.sourcePlane = Frame3();
    const auto planarLeft = rationalGraphPanel(-2., 0., 0., false);
    const auto nonlinearRight = rationalGraphPanel(0., 2., 4e-9, true, .125);
    const std::vector<SurfaceTargetFace> nonlinearTargets{{&planarLeft, planarLeft.faces().front()},
                                                         {&nonlinearRight, nonlinearRight.faces().front()}};
    const auto combined = wrapBodyOnFaces(source, nonlinearTargets, options, &history, &deviation);
    FK_CHECK(combined.isSheet() && combined.faces().size() == 1 && checkBody(combined, {true, true}).empty());
    if (combined.faces().size() != 1) return;
    const auto &combinedImage = *combined.face(combined.faces().front()).surface;
    const auto combinedProof = detail::projectedMappingResidualBound(source, sourceFace, nonlinearTargets, options,
        Frame3(), combinedImage, {-1., 1.}, {-1., 1.}, budget);
    FK_CHECK(combinedProof.status == detail::SurfaceMappingBoundStatus::Certified && combinedProof.upperBound <= budget);
    FK_CHECK(deviation >= combinedProof.upperBound && deviation <= options.tolerance);
    for (const auto &target : nonlinearTargets) {
        const auto &surface = *target.body->face(target.face).surface;
        const double u = target.body == &planarLeft ? -.5 : .5;
        for (double v : {-.5, .5}) {
            const auto hit = surface.point(u, v);
            if (std::abs(hit.x()) >= 1.) continue;
            const double height = (hit.x() * hit.x() - hit.y() * hit.y()) / 8192. + options.normalOffset;
            const auto expected = hit + height * surface.normal(u, v);
            FK_CHECK(distance(expected, combinedImage.point(hit.x(), hit.y())) <= combinedProof.upperBound + 1e-12);
        }
    }
    const auto combinedLost = detail::projectedMappingResidualBound(source, sourceFace, nonlinearTargets, options,
        Frame3(), wrong, {-1., 1.}, {-1., 1.}, budget);
    FK_CHECK(combinedLost.status == detail::SurfaceMappingBoundStatus::Failed);
    FK_CHECK(writeBodyBinary(source) == sourceBefore);
}

FK_TEST(MappingGlobalInclinedSourceOnNonlinearRationalChart) {
    const std::vector<double> ku{-1., -1., -1., 1., 1., 1.}, kv{-1., -1., 1., 1.};
    std::vector<Vec3> poles;
    constexpr double relief = 1. / 8192.;
    for (double u : {-1., 0., 1.}) for (double v : {-1., 1.})
        poles.emplace_back(u, v, u == 0. ? -relief : relief);
    const auto sourceGraph = std::make_shared<BSplineSurface>(2, 1, ku, kv, 3, 2, poles);
    const auto source = rectangularSplinePatch(sourceGraph);
    const auto sourceFace = source.faces().front();
    const auto left = rationalGraphPanel(-2., 0., 0., false);
    const std::vector<double> knotsU{0., 0., 2., 2.}, knotsV{-2., -2., 2., 2.};
    const auto rightGraph = std::make_shared<BSplineSurface>(1, 1, knotsU, knotsV, 2, 2,
        std::vector<Vec3>{{0.,-2.,0.}, {0.,2.,0.}, {2.,-2.,0.}, {2.,2.,0.}},
        std::vector<double>{1., 1., 2., 2.});
    const auto right = rectangularSplinePatch(rightGraph);
    FK_CHECK(std::abs(rightGraph->point(1., 0.).x() - 1.) > .1);
    const std::vector<SurfaceTargetFace> targets{{&left, left.faces().front()}, {&right, right.faces().front()}};
    auto options = wrapOptions();
    options.sourcePlane = Frame3({}, {0., .125, 1.}, {1., 0., 0.});
    const double budget = .2 * std::min(options.tolerance, linearResolution());
    double deviation = -1.;
    BodyHistory history;
    const auto result = wrapBodyOnFaces(source, targets, options, &history, &deviation);
    FK_CHECK(result.isSheet() && result.faces().size() == 1 && checkBody(result, {true, true}).empty());
    if (result.faces().size() != 1) return;
    checkFaceOrigins(source, result, history);
    const auto &image = *result.face(result.faces().front()).surface;
    const auto proof = detail::projectedMappingResidualBound(source, sourceFace, targets, options,
        Frame3(), image, {-1., 1.}, {-1., 1.}, budget);
    FK_CHECK(proof.status == detail::SurfaceMappingBoundStatus::Certified && proof.upperBound <= budget);
    FK_CHECK(proof.upperBound <= deviation);
    for (double u : {-.75, -.25, 0., .25, .75}) for (double v : {-.75, .25, .75}) {
        const auto expected = options.sourcePlane.toLocal(Vec3(u, v, relief * u * u));
        FK_CHECK(distance(expected, image.point(u, v)) <= deviation + 1e-12);
    }

    // Z = k X Y on both sides. The second chart still has a rational,
    // non-affine projection, and the two patches meet tangentially.
    constexpr double k = 1. / 32768.;
    const auto saddlePatch = [&](double first, double last, bool rational) {
        std::vector<Vec3> grid;
        for (double x : {first, last}) for (double y : {-2., 2.})
            grid.emplace_back(x, y, k * x * y);
        return rectangularSplinePatch(std::make_shared<BSplineSurface>(1, 1,
            std::vector<double>{first, first, last, last}, knotsV, 2, 2, grid,
            rational ? std::vector<double>{1., 1., 2., 2.} : std::vector<double>{}));
    };
    const auto curvedLeft = saddlePatch(-2., 0., false), curvedRight = saddlePatch(0., 2., true);
    const std::vector<SurfaceTargetFace> curvedTargets{{&curvedLeft, curvedLeft.faces().front()},
        {&curvedRight, curvedRight.faces().front()}};
    const auto curvedResult = wrapBodyOnFaces(source, curvedTargets, options, &history, &deviation);
    FK_CHECK(curvedResult.isSheet() && curvedResult.faces().size() == 1 && checkBody(curvedResult, {true, true}).empty());
    if (curvedResult.faces().size() != 1) return;
    checkFaceOrigins(source, curvedResult, history);
    const auto &curvedImage = *curvedResult.face(curvedResult.faces().front()).surface;
    const auto curvedProof = detail::projectedMappingResidualBound(source, sourceFace, curvedTargets, options,
        Frame3(), curvedImage, {-1., 1.}, {-1., 1.}, budget);
    FK_CHECK(curvedProof.status == detail::SurfaceMappingBoundStatus::Certified && curvedProof.upperBound <= budget);
    FK_CHECK(curvedProof.upperBound <= deviation);
    for (double u : {-.75, -.25, 0., .25, .75}) for (double v : {-.75, .25, .75}) {
        const auto local = options.sourcePlane.toLocal(Vec3(u, v, relief * u * u));
        const Vec3 contact(local.x(), local.y(), k * local.x() * local.y());
        const auto expected = contact + local.z() * normalized(Vec3(-k * local.y(), -k * local.x(), 1.));
        FK_CHECK(distance(expected, curvedImage.point(u, v)) <= deviation + 1e-12);
    }

    // Rotate the graph's height as usual in Z but omit its contribution in Y.
    const std::vector<double> knots{-1., -1., -1., -1., 1., 1., 1., 1.};
    const std::array<double, 4> q{-1., -1./3., 1./3., 1.}, z{1., -1./3., -1./3., 1.};
    poles.clear();
    for (std::size_t i = 0; i < q.size(); ++i) for (double v : q) {
        auto local = options.sourcePlane.toLocal(Vec3(q[i], v, relief * z[i]));
        local[1] = options.sourcePlane.yDir().y() * v;
        poles.push_back(local);
    }
    const BSplineSurface missingShift(3, 3, knots, knots, 4, 4, poles);
    FK_CHECK(distance(missingShift.point(.75, .5),
        options.sourcePlane.toLocal(Vec3(.75, .5, relief * .75 * .75))) > budget);
    const auto lost = detail::projectedMappingResidualBound(source, sourceFace, targets, options,
        Frame3(), missingShift, {-1., 1.}, {-1., 1.}, budget);
    FK_CHECK(lost.status == detail::SurfaceMappingBoundStatus::Failed);
}

FK_TEST(MappingGlobalCoupledInclinedSourceOnRationalChart) {
    const auto saddle = [](bool quadratic, double scale) {
        const auto coordinates = quadratic ? std::vector<double>{-1., 0., 1.} : std::vector<double>{-1., 1.};
        const auto square = std::array<double, 3>{1., -1., 1.};
        std::vector<Vec3> poles;
        for (std::size_t i = 0; i < coordinates.size(); ++i) for (std::size_t j = 0; j < coordinates.size(); ++j) {
            const double u = coordinates[i], v = coordinates[j];
            const double h = u * v + (quadratic ? .5 * (square[i] - square[j]) : 0.);
            poles.emplace_back(u, v, scale * h);
        }
        const int degree = quadratic ? 2 : 1;
        std::vector<double> knots(degree + 1, -1.);
        knots.insert(knots.end(), degree + 1, 1.);
        return rectangularSplinePatch(std::make_shared<BSplineSurface>(degree, degree, knots, knots,
            degree + 1, degree + 1, poles));
    };
    auto options = wrapOptions();
    options.sourcePlane = Frame3({}, {.125, -.125, 1.}, {1., 0., 0.});
    const double budget = .2 * std::min(options.tolerance, linearResolution());
    const auto &frame = options.sourcePlane;
    for (bool quadratic : {false, true}) {
        const double relief = quadratic ? 1. / 16384. : 1. / 8192.;
        const auto source = saddle(quadratic, relief);
        const auto face = source.faces().front();
        const auto local = [&](double u, double v) {
            const double h = u * v + (quadratic ? .5 * (u * u - v * v) : 0.);
            return frame.toLocal(Vec3(u, v, relief * h));
        };
        const auto point = local(.75, .625);
        const auto affine = frame.toLocal(Vec3(.75, .625, 0.));
        FK_CHECK(std::abs(point.x() - affine.x()) > 10. * budget);
        FK_CHECK(std::abs(point.y() - affine.y()) > 10. * budget);

        for (double k : {0., 1. / 32768.}) {
            const auto patch = [&](double left, double right, bool rational) {
                std::vector<Vec3> poles;
                for (double x : {left, right}) for (double y : {-2., 2.})
                    poles.emplace_back(x, y, k * x * y);
                return rectangularSplinePatch(std::make_shared<BSplineSurface>(1, 1,
                    std::vector<double>{left,left,right,right}, std::vector<double>{-2.,-2.,2.,2.},
                    2, 2, poles, rational ? std::vector<double>{1.,1.,2.,2.} : std::vector<double>{}));
            };
            const auto left = patch(-2., 0., false), right = patch(0., 2., true);
            const std::vector<SurfaceTargetFace> targets{{&left,left.faces().front()}, {&right,right.faces().front()}};
            const auto &target = *right.face(right.faces().front()).surface;
            FK_CHECK(std::abs(target.point(1., 0.).x() - 1.) > .1);
            double deviation = -1.;
            BodyHistory history;
            const auto wrapped = wrapBodyOnFaces(source, targets, options, &history, &deviation);
            FK_CHECK(wrapped.isSheet() && wrapped.faces().size() == 1 && checkBody(wrapped, {true,true}).empty());
            if (wrapped.faces().size() != 1) continue;
            checkFaceOrigins(source, wrapped, history);
            const auto &image = *wrapped.face(wrapped.faces().front()).surface;
            const auto proof = detail::projectedMappingResidualBound(source, face, targets,
                options, Frame3(), image, {-1.,1.}, {-1.,1.}, budget);
            FK_CHECK(proof.status == detail::SurfaceMappingBoundStatus::Certified && proof.upperBound <= budget);
            FK_CHECK(proof.upperBound <= deviation);
            for (double u : {-.875, -.25, .25, .875}) for (double v : {-.75, .375, .75}) {
                const auto p = local(u, v);
                const auto normal = normalized(Vec3(-k * p.y(), -k * p.x(), 1.));
                const Vec3 expected = Vec3(p.x(), p.y(), k * p.x() * p.y()) + p.z() * normal;
                FK_CHECK(distance(expected, image.point(u, v)) <= deviation + 1e-12);
            }
            if (k != 0.) continue;

            const std::vector<double> knots{-1.,-1.,-1.,-1.,1.,1.,1.,1.};
            std::vector<Vec3> poles;
            const std::array<double, 4> q{-1., -1./3., 1./3., 1.}, square{1., -1./3., -1./3., 1.};
            for (std::size_t i = 0; i < q.size(); ++i) for (std::size_t j = 0; j < q.size(); ++j) {
                const double u = q[i], v = q[j];
                const auto p = frame.toLocal(Vec3(u, v, 0.));
                const double h = u * v + (quadratic ? .5 * (square[i] - square[j]) : 0.);
                poles.emplace_back(p.x(), p.y(), frame.toLocal(Vec3(u, v, relief * h)).z());
            }
            const BSplineSurface lostShift(3, 3, knots, knots, 4, 4, poles);
            const auto lost = detail::projectedMappingResidualBound(source, face, targets,
                options, Frame3(), lostShift, {-1.,1.}, {-1.,1.}, budget);
            FK_CHECK(lost.status == detail::SurfaceMappingBoundStatus::Failed);
            const auto folded = saddle(quadratic, 16.);
            const auto unresolved = detail::projectedMappingResidualBound(folded, folded.faces().front(), targets,
                options, Frame3(), lostShift, {-1.,1.}, {-1.,1.}, budget);
            FK_CHECK(unresolved.status == detail::SurfaceMappingBoundStatus::Unsupported);
        }
    }
}

FK_TEST(MappingGlobalCubicAndSteepInclinedSource) {
    const std::vector<double> knots{-1.5,-1.5,-1.5,-1.5,1.5,1.5,1.5,1.5};
    const std::array<double, 4> coordinates{-1.5, -.5, .5, 1.5};
    const std::array<double, 4> cube{-3.375, 3.375, -3.375, 3.375};
    const std::array<double, 4> square{2.25, -.75, -.75, 2.25};
    constexpr double relief = 1. / 524288.;
    auto options = wrapOptions();
    options.sourcePlane = Frame3({}, {.125, -.125, 1.}, {1., 0., 0.});
    const auto &frame = options.sourcePlane;
    const double budget = .2 * std::min(options.tolerance, linearResolution());
    for (const double slope : {0., 6.}) {
        const double curvature = slope == 0. ? 1. / 16777216. : 0.;
        const double extent = slope == 0. ? 2. : 4.;
        const auto patch = [&](double left, double right, bool rational) {
            std::vector<Vec3> poles;
            for (double x : {left, right}) for (double y : {-extent, extent})
                poles.emplace_back(x, y, curvature * x * y);
            return rectangularSplinePatch(std::make_shared<BSplineSurface>(1, 1,
                std::vector<double>{left,left,right,right}, std::vector<double>{-extent,-extent,extent,extent},
                2, 2, poles, rational ? std::vector<double>{1.25,1.25,1.,1.} : std::vector<double>{}));
        };
        const auto left = patch(-extent, 0., false), right = patch(0., extent, true);
        const std::vector<SurfaceTargetFace> targets{{&left,left.faces().front()}, {&right,right.faces().front()}};
        const auto &target = *right.face(right.faces().front()).surface;
        FK_CHECK(std::abs(target.point(.5 * extent, 0.).x() - .5 * extent) > .05);

        std::vector<Vec3> poles, wrongPoles;
        for (std::size_t i = 0; i < coordinates.size(); ++i) for (std::size_t j = 0; j < coordinates.size(); ++j) {
            const double u = coordinates[i], v = coordinates[j], plane = slope * (v - u);
            const double z = plane + relief * (cube[i] + u * square[j]);
            poles.emplace_back(u, v, z);
            const auto p = frame.toLocal(Vec3(u, v, plane));
            wrongPoles.emplace_back(p.x(), p.y(), frame.toLocal(Vec3(u, v, z)).z());
        }
        const auto source = rectangularSplinePatch(std::make_shared<BSplineSurface>(3, 3,
            knots, knots, 4, 4, poles));
        const auto face = source.faces().front();
        const auto exactPoint = [&](double u, double v) {
            const auto p = frame.toLocal(Vec3(u, v, slope * (v - u) + relief * (u * u * u + u * v * v)));
            const auto normal = normalized(Vec3(-curvature * p.y(), -curvature * p.x(), 1.));
            return Vec3(p.x(), p.y(), curvature * p.x() * p.y()) + p.z() * normal;
        };
        if (slope != 0.) {
            // In world XY, the uncentered inverse has shift (-1/8,+1/8).
            // The directional derivative of the leading plane is 1.5.
            FK_CHECK(.25 * slope > 1.);
            const BSplineSurface lostRelief(3, 3, knots, knots, 4, 4, wrongPoles);
            FK_CHECK(distance(lostRelief.point(1.25, 1.), exactPoint(1.25, 1.)) > 10. * budget);
            const auto wrong = detail::projectedMappingResidualBound(source, face, targets,
                options, Frame3(), lostRelief, {-1.5,1.5}, {-1.5,1.5}, budget);
            FK_CHECK(wrong.status == detail::SurfaceMappingBoundStatus::Failed);
        }
        double deviation = -1.;
        BodyHistory history;
        const auto wrapped = wrapBodyOnFaces(source, targets, options, &history, &deviation);
        FK_CHECK(wrapped.isSheet() && wrapped.faces().size() == 1 && checkBody(wrapped, {true,true}).empty());
        if (wrapped.faces().size() != 1) continue;
        checkFaceOrigins(source, wrapped, history);
        const auto &image = *wrapped.face(wrapped.faces().front()).surface;
        const auto proof = detail::projectedMappingResidualBound(source, face, targets,
            options, Frame3(), image, {-1.5,1.5}, {-1.5,1.5}, budget);
        FK_CHECK(proof.status == detail::SurfaceMappingBoundStatus::Certified && proof.upperBound <= budget);
        FK_CHECK(proof.upperBound <= deviation);
        for (const double u : {-1.25, -.25, .75, 1.25}) for (const double v : {-1., .125, 1.})
            FK_CHECK(distance(exactPoint(u, v), image.point(u, v)) <= deviation + 1e-12);
    }
}

namespace {
void checkForwardSourceOnRationalCharts(int supportDegree, bool joined = false) {
    const bool squareX = joined || supportDegree == 4, squareY = supportDegree >= 3;
    const auto panel = [&](double lo, double hi, bool rational, double curvature, double level = 0., double slope = 0.) {
        const int pu = squareX ? 2 : 1, pv = squareY ? 2 : 1;
        const std::array<double, 3> x{lo, .5 * (lo + hi), hi}, y{-5., 0., 5.};
        const std::array<double, 3> xx{lo * lo, lo * hi, hi * hi}, yy{25., -25., 25.};
        const std::array<double, 3> weight{1., 2., 4.};
        std::vector<Vec3> poles;
        std::vector<double> weights;
        for (int i = 0; i <= pu; ++i) for (int j = 0; j <= pv; ++j) {
            const double px = x[squareX ? i : 2 * i], py = y[squareY ? j : 2 * j];
            const double transverse = squareY ? yy[j] : joined ? 1. : py;
            poles.emplace_back(px, py, level + slope * px + curvature * (squareX ? xx[i] : px) * transverse);
            if (rational) weights.push_back(squareX ? weight[i] : (i == 0 ? 1.25 : 1.));
        }
        std::vector<double> ku(pu + 1, lo), kv(pv + 1, -5.);
        ku.insert(ku.end(), pu + 1, hi);
        kv.insert(kv.end(), pv + 1, 5.);
        return rectangularSplinePatch(std::make_shared<BSplineSurface>(pu, pv, ku, kv, pu + 1, pv + 1, poles, weights));
    };
    auto options = wrapOptions();
    options.sourcePlane = Frame3({}, {.125, joined ? 0. : -.125, 1.}, {1., 0., 0.});
    options.normalOffset = joined ? 0. : .125;
    const auto &frame = options.sourcePlane;
    const Vec3 offset(0., 0., options.normalOffset);
    const double budget = .2 * std::min(options.tolerance, linearResolution());
    constexpr Interval domain{-1.5, 1.5};
    const auto knots = [domain](int degree) {
        std::vector<double> out(degree + 1, domain.lo);
        out.insert(out.end(), degree + 1, domain.hi);
        return out;
    };
    const std::array<double, 4> cubicUV{-1.5, -.5, .5, 1.5};
    const std::array<double, 4> cubed{-3.375, 3.375, -3.375, 3.375};
    const auto sourcePatch = [&](bool quartic, bool folded = false) {
        const int degree = quartic ? 4 : 3;
        std::vector<Vec3> poles;
        for (int i = 0; i <= degree; ++i) for (double v : {domain.lo, domain.hi}) {
            const double u = quartic ? domain.lo + .75 * i : cubicUV[i];
            const double h = quartic ? (i % 2 ? -1. : 1.) * 5.0625 / 8388608. :
                (folded ? 1. : -1.) * (3. * cubed[i] + 2. * u);
            poles.emplace_back(u, v, h);
        }
        return rectangularSplinePatch(std::make_shared<BSplineSurface>(degree, 1, knots(degree), knots(1),
            degree + 1, 2, poles));
    };
    std::vector<Vec3> noRelief;
    for (double u : cubicUV) for (double v : cubicUV) noRelief.push_back(frame.toLocal(Vec3(u, v, 0.)) + offset);
    const BSplineSurface wrongImage(3, 3, knots(3), knots(3), 4, 4, noRelief);
    for (bool quartic : {false, true}) {
        const double curvature = supportDegree == 0 ? 0. : supportDegree == 2
            ? (quartic ? 1. / 131072. : 1. / 8388608.)
            : (quartic ? 1. / 1048576. : 1. / 134217728.);
        const double leftCurvature = joined ? .5 * curvature : curvature;
        const auto left = panel(-5., 0., false, leftCurvature), right = panel(0., 5., true, curvature);
        const std::vector<SurfaceTargetFace> targets{{&left,left.faces().front()}, {&right,right.faces().front()}};
        const auto &target = *right.face(right.faces().front()).surface;
        FK_CHECK(std::abs(target.point(2.5, 0.).x() - 2.5) > .1);
        if (supportDegree != 0) FK_CHECK(std::abs(target.point(2.5, 3.).z()) > budget);
        const auto source = sourcePatch(quartic);
        const auto face = source.faces().front();
        const auto expected = [&](double u, double v) {
            const double h = quartic ? u * u * u * u / 8388608. : -3. * u * u * u - 2. * u;
            const auto p = frame.toLocal(Vec3(u, v, h));
            const double x = p.x(), y = p.y();
            const double k = x < 0. ? leftCurvature : curvature;
            const double gx = squareX ? x * x : x, gy = squareY ? y * y : joined ? 1. : y;
            const auto normal = normalized(Vec3(-k * (squareX ? 2. * x : 1.) * gy,
                -k * gx * (squareY ? 2. * y : joined ? 0. : 1.), 1.));
            return Vec3(x, y, k * gx * gy) + (p.z() + options.normalOffset) * normal;
        };
        FK_CHECK(distance(wrongImage.point(1.25, .25), expected(1.25, .25)) > budget);
        const auto wrong = detail::projectedMappingResidualBound(source, face, targets, options, Frame3(),
            wrongImage, domain, domain, budget);
        FK_CHECK(wrong.status == detail::SurfaceMappingBoundStatus::Failed);
        if (!quartic) {
            // The directional derivative k·grad(z) reaches 89/32 with
            // k=(-1/8,+1/8). Removing the tangent at the center leaves
            // a maximum of 81/40, even after rescaling the shift.
            const double cubicRate = 9. * domain.hi * domain.hi;
            FK_CHECK((cubicRate + 2.) / 8. > 1. && cubicRate / 10. > 1.);
            const auto folded = sourcePatch(false, true);
            const auto rejected = detail::projectedMappingResidualBound(folded, folded.faces().front(), targets,
                options, Frame3(), wrongImage, domain, domain, budget);
            FK_CHECK(rejected.status == detail::SurfaceMappingBoundStatus::Unsupported);
        } else {
            const auto lifted = panel(0., 5., true, curvature, .01);
            const std::vector<SurfaceTargetFace> differentLevels{{&left,left.faces().front()},
                {&lifted,lifted.faces().front()}};
            const auto rejected = detail::projectedMappingResidualBound(source, face, differentLevels, options,
                Frame3(), wrongImage, domain, domain, budget);
            FK_CHECK(rejected.status == detail::SurfaceMappingBoundStatus::Unsupported);
        }
        if (joined) {
            const auto crease = panel(0., 5., true, curvature, 0., 1. / 1024.);
            const auto gap = panel(.125, 5.125, true, curvature);
            for (const auto *invalid : {&crease, &gap}) {
                const std::vector<SurfaceTargetFace> brokenJoin{{&left,left.faces().front()},
                    {invalid,invalid->faces().front()}};
                const auto rejected = detail::projectedMappingResidualBound(source, face, brokenJoin, options,
                    Frame3(), wrongImage, domain, domain, budget);
                FK_CHECK(rejected.status == detail::SurfaceMappingBoundStatus::Unsupported);
            }
        }
        double deviation = -1.;
        BodyHistory history;
        const auto wrapped = wrapBodyOnFaces(source, targets, options, &history, &deviation);
        FK_CHECK(wrapped.isSheet() && wrapped.faces().size() == 1 && checkBody(wrapped, {true,true}).empty());
        if (wrapped.faces().size() != 1) continue;
        checkFaceOrigins(source, wrapped, history);
        const auto &image = *wrapped.face(wrapped.faces().front()).surface;
        const auto proof = detail::projectedMappingResidualBound(source, face, targets, options, Frame3(),
            image, domain, domain, budget);
        FK_CHECK(proof.status == detail::SurfaceMappingBoundStatus::Certified && proof.upperBound <= budget);
        FK_CHECK(proof.upperBound <= deviation);
        if (joined) {
            const std::vector<SurfaceTargetFace> reversed{targets.rbegin(), targets.rend()};
            const auto reverse = detail::projectedMappingResidualBound(source, face, reversed, options, Frame3(),
                image, domain, domain, budget);
            FK_CHECK(reverse.status == detail::SurfaceMappingBoundStatus::Certified && reverse.upperBound <= budget);
        }
        for (double u : {-1.25, -.5, .125, .625, 1.25}) for (double v : {-1., .125, 1.})
            FK_CHECK(distance(expected(u, v), image.point(u, v)) <= deviation + 1e-12);
    }
}
}

FK_TEST(MappingGlobalForwardSourceOnCoplanarRationalCharts) {
    checkForwardSourceOnRationalCharts(0);
}

FK_TEST(MappingGlobalForwardSourceOnCurvedRationalCharts) {
    checkForwardSourceOnRationalCharts(2);
}

FK_TEST(MappingGlobalForwardSourceOnHigherDegreeCharts) {
    checkForwardSourceOnRationalCharts(3);
    checkForwardSourceOnRationalCharts(4);
}

FK_TEST(MappingGlobalForwardQuarticSourceOnQuinticCharts) {
    constexpr double relief = 1. / 16777216.;
    constexpr Interval domain{-1., 1.};
    const std::vector<double> ku{-1., -1., -1., -1., -1., 1., 1., 1., 1., 1.};
    const std::vector<double> kv{-1., -1., 1., 1.};
    std::vector<Vec3> poles;
    for (int i = 0; i <= 4; ++i) for (double v : {-1., 1.})
        poles.emplace_back(-1. + .5 * i, v, i % 2 ? -relief : relief);
    const auto source = rectangularSplinePatch(std::make_shared<BSplineSurface>(4, 1, ku, kv, 5, 2, poles));
    const auto left = quinticOverlapPanel(-2., 2., 1., false), right = quinticOverlapPanel(-.5, 3.5, 1., true);
    FK_CHECK(checkBody(source, {true, true}).empty());
    const std::vector<SurfaceTargetFace> targets{{&left, left.faces().front()}, {&right, right.faces().front()}};
    auto options = wrapOptions();
    options.sourcePlane = Frame3({}, {.125, -.125, 1.}, {1., 0., 0.});
    options.normalOffset = .0625;
    const double budget = .2 * std::min(options.tolerance, linearResolution());
    const auto expected = [&](double u, double v) {
        const auto p = options.sourcePlane.toLocal(Vec3(u, v, relief * u * u * u * u));
        const double x = p.x(), y = p.y(), native = x - quinticShear * y * y;
        const double dx = 2. * quinticHeightScale * native * y;
        const double dy = quinticHeightScale * (native * native - 4. * quinticShear * native * y * y);
        const auto normal = normalized(Vec3(-dx, -dy, 1.));
        return Vec3(x, y, quinticHeightScale * native * native * y) + (p.z() + options.normalOffset) * normal;
    };

    double deviation = -1.;
    const auto wrapped = wrapBodyOnFaces(source, targets, options, nullptr, &deviation);
    FK_CHECK(wrapped.isSheet() && wrapped.faces().size() == 1 && checkBody(wrapped, {true, true}).empty());
    if (wrapped.faces().size() != 1) return;
    const auto *image = dynamic_cast<const BSplineSurface *>(wrapped.face(wrapped.faces().front()).surface.get());
    FK_CHECK(image && !image->isRational());
    if (!image || image->isRational()) return;
    const auto prove = [&](const Surface &carrier) {
        return detail::projectedMappingResidualBound(source, source.faces().front(), targets, options,
            Frame3(), carrier, domain, domain, budget);
    };
    const auto proof = prove(*image);
    FK_CHECK(proof.status == detail::SurfaceMappingBoundStatus::Certified && proof.upperBound <= budget);
    FK_CHECK(proof.upperBound <= deviation);
    for (double u : {-.75, 0., .75}) for (double v : {-.75, 0., .75})
        FK_CHECK(distance(image->point(u, v), expected(u, v)) <= deviation + 1e-12);

    // This carrier preserves the topology and all parameter spans, but
    // moves the geometry away from the physical support and normal offset.
    poles.clear();
    for (int i = 0; i < image->uPoleCount(); ++i) for (int j = 0; j < image->vPoleCount(); ++j)
        poles.push_back(image->pole(i, j) + Vec3(0., 0., 1. / 65536.));
    const BSplineSurface wrong(image->uDegree(), image->vDegree(), image->uKnots(), image->vKnots(),
                              image->uPoleCount(), image->vPoleCount(), poles);
    FK_CHECK(distance(wrong.point(.75, .75), expected(.75, .75)) > budget);
    FK_CHECK(prove(wrong).status == detail::SurfaceMappingBoundStatus::Failed);
}

FK_TEST(MappingGlobalForwardSourceOnJoinedQuadraticCharts) {
    checkForwardSourceOnRationalCharts(2, true);
}
