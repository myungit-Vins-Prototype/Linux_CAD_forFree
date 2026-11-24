#include <algorithm>

#include "fk_test.h"

#include <set>

#include "fk_body_check.h"
#include "fk_body_io.h"
#include "fk_boolean.h"
#include "fk_classify.h"
#include "fk_mass.h"
#include "fk_primitives.h"
#include "fk_quantize.h"
#include "fk_sheet_gyroid.h"
#include "fk_tessellate.h"
#include "fk_surface.h"
#include "fk_unify.h"

using namespace ForgeCad::Kernel;

FK_TEST(QuantizeKeepsAnalyticBandBetweenTwoZones) {
    constexpr double radius = 9.15;
    const Body cylinder = makeCylinder(Frame3(), radius, 28);
    const Box lower{{-11, -11, -1}, {11, 11, 9.1}};
    const Box upper{{-11, -11, 18.2}, {11, 11, 29}};
    const Body coarse = quantizeSolid(cylinder, 3.0, {lower, upper});
    const Body fine = quantizeSolid(cylinder, 1.5, {lower, upper});
    for (const Body *body : {&coarse, &fine}) {
        FK_CHECK(checkBody(*body, {true, true}).empty());
        FK_CHECK(body->shells().size() == 1);
        double cylindricalArea = 0.0;
        for (FaceId face : body->faces())
            if (body->face(face).surface->type() == SurfaceType::Cylinder)
                cylindricalArea += faceArea(*body, face);
        FK_CHECK_NEAR(cylindricalArea, kTwoPi * radius * (18.2 - 9.1), 1e-7);
    }
    FK_CHECK(fine.faces().size() > coarse.faces().size());
}

FK_TEST(QuantizeKeepsThroughHoleInOneSolid) {
    const Body outer = makeCylinder(Frame3(), 9.15, 28);
    const Body tool = makeCylinder(Frame3({0, 0, -1}, {0, 0, 1}, {1, 0, 0}), 2.9, 30);
    const Body hollow = booleanOperation(outer, tool, BooleanOperation::Subtract);
    const Box upper{{-11, -11, 18.2}, {11, 11, 29}};
    const Body full = quantizeSolid(hollow, 1.5);
    const Body partial = quantizeSolid(hollow, 1.5, {upper});
    for (const Body *result : {&full, &partial}) {
        FK_CHECK(checkBody(*result, {true, true}).empty());
        const auto shells = result->shells();
        FK_CHECK(shells.size() == 1);
        if (shells.size() == 1) FK_CHECK(shellGenus(*result, shells.front()) == 1);
        const SolidClassifier classifier(*result, booleanConnectionTolerance(), true);
        for (double z : {8.0, 23.0}) {
            FK_CHECK(classifier.classify({0, 0, z}) == PointLocation::Outside);
            FK_CHECK(classifier.classify({5, 0, z}) == PointLocation::Inside);
        }
    }
}

FK_TEST(QuantizeRejectsDisconnectedGridAndInvalidZones) {
    const Body first = makeBox(Frame3(), 3, 3, 3);
    const Body second = makeBox(Frame3({8, 0, 0}, {0, 0, 1}, {1, 0, 0}), 3, 3, 3);
    const Body separated = Body::collect({&first, &second});
    FK_CHECK_THROWS(quantizeSolid(separated, 1));
    const Body bridge = makeBox(Frame3({2, 0.1, 0.1}, {0, 0, 1}, {1, 0, 0}), 7, 0.15, 0.15);
    const Body connected = booleanOperation(booleanOperation(first, bridge, BooleanOperation::Unite),
                                            second, BooleanOperation::Unite);
    FK_CHECK(connected.shells().size() == 1);
    FK_CHECK_THROWS(quantizeSolid(connected, 1));
    FK_CHECK_THROWS(quantizeSolid(first, 0));
    FK_CHECK_THROWS(quantizeSolid(first, 0.003));
    FK_CHECK_THROWS(quantizeSolid(first, 1, {{{-1, -1, -1}, {2, 4, 4}},
                                           {{1, -1, -1}, {4, 4, 4}}}));
}

FK_TEST(LatticeOpensPoresAndRemainsJoinedToTheBase) {
    const Body box = makeBox(Frame3(), 6, 6, 9);
    const Box head{{-1, -1, 3}, {7, 7, 10}};
    const CubicLattice pattern{{0.375, 0.375, 3.125}, 1.5, 0.6};
    const Body result = latticeSolid(box, 0.25, pattern, {head});
    FK_CHECK(result.shells().size() == 1);
    const SolidClassifier classifier(result, booleanConnectionTolerance(), true);
    FK_CHECK(classifier.classify({1.0, 1.0, 1.0}) == PointLocation::Inside);
    FK_CHECK(classifier.classify({1.0, 1.0, 5.25}) == PointLocation::Outside);
    FK_CHECK(classifier.classify({0.375, 0.375, 5.25}) == PointLocation::Inside);
    FK_CHECK(massProperties(result).volume < massProperties(box).volume);
    FK_CHECK_THROWS(latticeSolid(box, 0.8, pattern));
}

FK_TEST(WovenLatticeAlternatesCrossingsAndKeepsTheBase) {
    const Body box = makeBox(Frame3(), 3.2, 3.2, 3.2);
    const Box head{{-1, -1, 1.6}, {7, 7, 4}};
    const WovenLattice pattern{{{0.1, 0.1, 0.1}, 1.6, 0.52}, 0.4, 0.46};
    const Body result = wovenLatticeSolid(box, 0.2, pattern, {head});
    FK_CHECK(result.shells().size() == 1);
    FK_CHECK(checkBody(result, {true, true}).empty());

    const SolidClassifier classifier(result, booleanConnectionTolerance(), true);
    FK_CHECK(classifier.classify({1.0, 1.0, 0.8}) == PointLocation::Inside);
    for (const double x : {0.9, 2.5}) {
        FK_CHECK(classifier.classify({x, 1.7, 2.1}) == PointLocation::Inside);
        FK_CHECK(classifier.classify({x, 1.7, 2.5}) == PointLocation::Outside);
        FK_CHECK(classifier.classify({x, 1.7, 2.9}) == PointLocation::Inside);
    }

    // Lo stesso filo lungo X passa sotto al primo incrocio e sopra al secondo.
    FK_CHECK(classifier.classify({1.22, 1.7, 2.1}) == PointLocation::Inside);
    FK_CHECK(classifier.classify({1.22, 1.7, 2.9}) == PointLocation::Outside);
    FK_CHECK(classifier.classify({2.82, 1.7, 2.1}) == PointLocation::Outside);
    FK_CHECK(classifier.classify({2.82, 1.7, 2.9}) == PointLocation::Inside);

    WovenLattice detached = pattern;
    detached.frame.strutWidth = 0.22;
    detached.filamentWidth = 0.30;
    detached.amplitude = 0.30;
    FK_CHECK_THROWS(wovenLatticeSolid(box, 0.2, detached, {head}));
}

FK_TEST(WovenLatticeOpensOppositeVoidsAtAVertex) {
    Body source = makeBox(Frame3(), 3.2, 3.2, 3.2);
    // Due cavita' separate nel B-rep occupano ottanti opposti di uno
    // stesso vertice quando vengono campionate su celle da 0,2 mm.
    for (const double q : {0.65, 0.85}) {
        const Body cavity = makeBox(Frame3({q, q, q}, {0, 0, 1}, {1, 0, 0}), 0.10, 0.10, 0.10);
        source = booleanOperation(source, cavity, BooleanOperation::Subtract);
    }
    const WovenLattice pattern{{{0.8, 0.8, 0.8}, 1.6, 0.52}, 0.4, 0.46};
    const Body result = wovenLatticeSolid(source, 0.2, pattern);
    FK_CHECK(checkBody(result, {true, true}).empty());
    FK_CHECK(result.shells().size() == 1);
    const SolidClassifier classifier(result, booleanConnectionTolerance(), true);
    FK_CHECK(classifier.classify({0.7, 0.7, 0.7}) == PointLocation::Outside);
    FK_CHECK(classifier.classify({0.9, 0.9, 0.9}) == PointLocation::Outside);
    FK_CHECK(classifier.classify({0.8, 0.8, 1.6}) == PointLocation::Inside);
}

FK_TEST(UnifyKeepsPlanarPatchesWithAPinchedBoundary) {
    const BooleanOptions options{booleanConnectionTolerance(), false};
    Body stepped = makeBox(Frame3(), 3.0, 3.0, 0.5);
    for (int x = 0; x < 3; ++x)
        for (int y = 0; y < 3; ++y) {
            if (x == 0 && y == 0) continue;
            // La pila centrale e l'angolo ribassato si toccano in
            // pianta, alla quota delle facce circostanti.
            const Frame3 frame({double(x), double(y), 0.5}, {0, 0, 1}, {1, 0, 0});
            const Body cell = makeBox(frame, 1.0, 1.0, x == 1 && y == 1 ? 1.0 : 0.5);
            stepped = booleanOperation(stepped, cell, BooleanOperation::Unite, options);
        }
    const Body compact = unifySameDomain(stepped);
    FK_CHECK(compact.faces().size() < stepped.faces().size());
    FK_CHECK(checkBody(compact, {true, true}).empty());
    FK_CHECK_NEAR(massProperties(compact).volume, massProperties(stepped).volume, 1e-9);
    for (FaceId face : compact.faces()) {
        std::set<int> boundaryVertices;
        for (LoopId loop : compact.face(face).loops)
            for (FinId fin : compact.loopFins(loop))
                FK_CHECK(boundaryVertices.insert(compact.finStart(fin).index).second);
    }
}

FK_TEST(WovenLatticeKeepsExactAxialLimit) {
    const double maximum = 6.17;
    const Body source = makeCylinder(Frame3(), 2.35, maximum);
    const WovenLattice pattern{{{-2.25, -2.25, 2.13}, 1.6, 0.52}, 0.4, 0.46};
    const Box region{{-3.0, -3.0, 2.03}, {3.0, 3.0, maximum}};
    const Body result = wovenLatticeSolid(source, 0.2, pattern, {region});
    FK_CHECK(checkBody(result, {true, true}).empty());
    FK_CHECK(result.shells().size() == 1);
    double top = region.lo.z();
    for (VertexId id : result.vertices()) top = std::max(top, result.vertex(id).point.z());
    FK_CHECK_NEAR(top, maximum, 1e-10);

    // La parte di cilindro fuori dalla selezione coincide con il fusto analitico.
    const Body stemMask = makeBox(Frame3({-3, -3, -1}, {0, 0, 1}, {1, 0, 0}), 6.0, 6.0, 3.0);
    const Body stem = booleanOperation(result, stemMask, BooleanOperation::Intersect);
    FK_CHECK_NEAR(massProperties(stem).volume, kPi * 2.35 * 2.35 * 2.0, 1e-7);
}

FK_TEST(WovenLatticeSuggestionsRespectTheGridBudget) {
    for (const Vec3 size : {Vec3{3.2, 3.2, 6.17}, Vec3{5.35, 14.14, 6.65},
                            Vec3{0.06, 240.0, 12.0}, Vec3{10000.0, 2200.0, 900.0}}) {
        const Box zone{{}, size};
        const auto settings = suggestWovenLattice(zone);
        validateWovenLattice(zone, settings);
        FK_CHECK(settings.samplingPitch > booleanConnectionTolerance());
        FK_CHECK(wovenLatticeMaterial(settings.pattern.frame.origin, settings.pattern));
        const double cells = std::ceil(size.x() / settings.samplingPitch)
            * std::ceil(size.y() / settings.samplingPitch) * std::ceil(size.z() / settings.samplingPitch);
        FK_CHECK(cells <= 80000);
    }
    const Body cube = makeBox(Frame3(), 3.2, 3.2, 3.2);
    const auto settings = suggestWovenLattice({{}, {3.2, 3.2, 3.2}});
    const Body result = wovenLatticeSolid(cube, settings.samplingPitch, settings.pattern);
    FK_CHECK(result.shells().size() == 1);
    FK_CHECK(checkBody(result, {true, true}).empty());
    FK_CHECK_THROWS(suggestWovenLattice({{}, {0, 1, 1}}));
}

FK_TEST(SheetGyroidKeepsAnAttachedWallAndAFullBase) {
    const Body source = makeBox(Frame3(), 3.2, 3.2, 3.2);
    const Box head{{-.2, -.2, 1.0}, {3.4, 3.4, 3.4}};
    const SheetGyroid pattern{{1.6, 1.6, 1.0}, 2.5, 0.42};
    const Vec3 alongDiagonal{0.3125, 0.3125, 0.3125};
    FK_CHECK(sheetGyroidMaterial(pattern.origin, pattern));
    FK_CHECK(sheetGyroidMaterial(pattern.origin + Vec3{2.5, 0, 0}, pattern));
    FK_CHECK(!sheetGyroidMaterial(pattern.origin + alongDiagonal, pattern));
    FK_CHECK(!sheetGyroidMaterial(pattern.origin - alongDiagonal, pattern));
    FK_CHECK_THROWS(sheetGyroidSolid(source, .22, pattern, {head}));
    const Body result = sheetGyroidSolid(source, .18, pattern, {head});
    FK_CHECK(!result.isSheet());
    FK_CHECK(result.shells().size() == 1);
    FK_CHECK(checkBody(result, {true, true}).empty());
    const SolidClassifier classifier(result, booleanConnectionTolerance(), true);
    FK_CHECK(classifier.classify({1.6, 1.6, .5}) == PointLocation::Inside);
    const double volume = massProperties(result).volume;
    FK_CHECK(volume > 3.2 * 3.2);
    FK_CHECK(volume < massProperties(source).volume);
}

FK_TEST(CurvedGyroidInterpolatesAWallBeyondTheEditableStart) {
    const Body source = makeBox(Frame3(), 3.2, 3.2, 3.2);
    const SheetGyroid pattern{{1.6, 1.6, 1.0}, 2.5, 0.42};
    FK_CHECK_THROWS(curvedGyroidSolid(source, .16, pattern, 2, 1.0, 3.2));
    const Body result = curvedGyroidSolid(source, .125, pattern, 2, 1.0, 3.2);
    FK_CHECK(result.shells().size() == 1);
    FK_CHECK(checkBody(result, {true, true}).empty());

    const SolidClassifier classifier(result, booleanConnectionTolerance());
    FK_CHECK(classifier.classify({1.6, 1.6, .5}) == PointLocation::Inside);
    FK_CHECK(classifier.classify({1.87, 1.87, 1.27}) == PointLocation::Outside);
    // La parete interpola il livello e non è l'orlo di voxel cartesiani.
    const auto faces = result.faces();
    const auto nOblique = std::count_if(faces.begin(), faces.end(), [&](FaceId id) {
        const auto &plane = static_cast<const Plane &>(*result.face(id).surface);
        const Vec3 n = plane.frame().zDir();
        return std::max({std::abs(n.x()), std::abs(n.y()), std::abs(n.z())}) < .98;
    });
    FK_CHECK(nOblique > 100);
    const Body later = curvedGyroidSolid(source, .125, pattern, 2, 1.5, 3.2);
    const SolidClassifier laterClassifier(later, booleanConnectionTolerance());
    FK_CHECK(laterClassifier.classify({1.87, 1.87, 1.27}) == PointLocation::Inside);
    FK_CHECK(massProperties(later).volume > massProperties(result).volume);
}

FK_TEST(SmallStraightEdgesRespectTheRequestedDensity) {
    const Line<3> line({}, {1.0, 0.0, 0.0});
    const Interval range{0.0, 0.002};
    TessellationOptions options;
    options.deflection = 0.01;
    FK_CHECK(sampleCurve(line, range, options).size() == 2);
    options.maxEdgeLength = 0.001;
    FK_CHECK(sampleCurve(line, range, options).size() == 3);
}

FK_TEST(CurvedGyroidPreservesTheExactCarrierOutsideBothLimits) {
    const Body source = makeCylinder(Frame3(), 1.6, 3.2);
    const SheetGyroid pattern{{0.0, 0.0, .8}, 2.5, .42};
    const Body result = curvedGyroidSolid(source, .125, pattern, 2, .8, 2.45);
    FK_CHECK(result.shells().size() == 1);
    FK_CHECK(checkBody(result, {true, true}).empty());
    bool lower = false, upper = false;
    int cylinders = 0;
    for (const FaceId face : result.faces()) {
        const auto &surface = *result.face(face).surface;
        if (surface.type() != SurfaceType::Cylinder) continue;
        ++cylinders;
        const auto &cylinder = static_cast<const CylindricalSurface &>(surface);
        FK_CHECK_NEAR(cylinder.radius(), 1.6, 1e-12);
        double lo = std::numeric_limits<double>::infinity(), hi = -lo;
        for (const LoopId loop : result.face(face).loops)
            for (const FinId fin : result.loopFins(loop)) {
                const double z = result.vertex(result.finStart(fin)).point.z();
                lo = std::min(lo, z);
                hi = std::max(hi, z);
            }
        lower = lower || (std::abs(lo) < 1e-6 && std::abs(hi - .8) < 1e-6);
        upper = upper || (std::abs(lo - 2.45) < 1e-6 && std::abs(hi - 3.2) < 1e-6);
    }
    FK_CHECK(cylinders == 2);
    FK_CHECK(lower && upper);
}

FK_TEST(CurvedGyroidKeepsOpeningsOnAllSixOriginalBoxPlanes) {
    const Body source = makeBox(Frame3(), 5.0, 5.0, 5.0);
    const SheetGyroid pattern{{2.89, 2.79, 2.87}, 2.5, .42};
    const Body result = curvedGyroidSolid(source, .125, pattern);
    FK_CHECK(result.shells().size() == 1);
    FK_CHECK(checkBody(result, {true, true}).empty());
    const auto resultFaces = result.faces();
    for (const FaceId face : source.faces()) {
        const auto &support = source.face(face).surface;
        FK_CHECK(std::any_of(resultFaces.begin(), resultFaces.end(), [&](FaceId patch) {
            return result.face(patch).surface == support;
        }));
    }
    const SolidClassifier classifier(result, booleanConnectionTolerance());
    FK_CHECK(classifier.classify({.025, 2.5, 2.5}) == PointLocation::Outside);
    FK_CHECK(classifier.classify({.025, 2.0, 2.0}) == PointLocation::Inside);
}

FK_TEST(CurvedGyroidUsesSourceCrossingsAtTheCylindricalBoundary) {
    const Body source = makeCylinder(Frame3(), 1.6, 5.0);
    const SheetGyroid pattern{{.13, -.19, .21}, 2.5, .52};
    const Body result = curvedGyroidSolid(source, .125, pattern);
    FK_CHECK(result.shells().size() == 1);
    FK_CHECK(checkBody(result, {true, true}).empty());

    int onCylinder = 0;
    for (const VertexId vertex : result.vertices()) {
        const Vec3 point = result.vertex(vertex).point;
        if (point.z() > .125 && point.z() < 4.875 &&
            std::abs(std::hypot(point.x(), point.y()) - 1.6) < 1e-9)
            ++onCylinder;
    }
    FK_CHECK(onCylinder > 100);
}

FK_TEST(SplineGyroidExchangesACurvedWallWithTheOriginalSolidBase) {
    const Body source = makeCylinder(Frame3(), 1.6, 3.2);
    const SheetGyroid pattern{{0.0, 0.0, .8}, 2.5, .42};
    const auto envelope = sampleCurvedGyroidEnvelope(source, .125);
    const Body faceted = curvedGyroidSolid(source, *envelope, pattern, 2, .8, 2.45);
    const Body spline = curvedGyroidSolid(source, *envelope, pattern, 2, .8, 2.45, GyroidWall::Spline);
    FK_CHECK(!spline.isSheet());
    FK_CHECK(spline.shells().size() == 1);
    FK_CHECK(checkBody(spline, {true, true}).empty());
    FK_CHECK(spline.vertices().size() == faceted.vertices().size());
    FK_CHECK(spline.edges().size() == faceted.edges().size());
    FK_CHECK(spline.faces().size() == faceted.faces().size());
    const auto faces = spline.faces();
    FK_CHECK(std::count_if(faces.begin(), faces.end(), [&](FaceId f) {
        return spline.face(f).surface->type() == SurfaceType::BSpline;
    }) > 100);
    const auto edges = spline.edges();
    FK_CHECK(std::count_if(edges.begin(), edges.end(), [&](EdgeId e) {
        return spline.edge(e).curve->type() == CurveType::BSpline;
    }) > 100);
    int retained = 0;
    for (const FaceId f : faces) {
        const auto carrier = spline.face(f).surface;
        if (carrier->type() != SurfaceType::Cylinder) continue;
        ++retained;
        FK_CHECK_NEAR(static_cast<const CylindricalSurface &>(*carrier).radius(), 1.6, 1e-12);
    }
    FK_CHECK(retained == 2);
    const SolidClassifier inside(spline, booleanConnectionTolerance());
    FK_CHECK(inside.classify({0, 0, .5}) == PointLocation::Inside);

    const Body decoded = readBodyBinary(writeBodyBinary(spline));
    FK_CHECK(decoded.shells().size() == 1);
    FK_CHECK(checkBody(decoded, {true, true}).empty());
    const auto decodedFaces = decoded.faces();
    FK_CHECK(std::count_if(decodedFaces.begin(), decodedFaces.end(), [&](FaceId f) {
        return decoded.face(f).surface->type() == SurfaceType::BSpline;
    }) > 100);
}


FK_TEST(TrimmedSplineGyroidKeepsTheBoxSupportsAndHoles) {
    const Body source = makeBox(Frame3(), 5., 5., 5.);
    const SheetGyroid pattern{{2.38, 2.61, 2.47}, 4., 1.3};
    const Body result = trimmedSplineGyroidSolid(source, .40, pattern);
    FK_CHECK(!result.isSheet());
    FK_CHECK(result.shells().size() == 1);
    FK_CHECK(checkBody(result, {true, true}).empty());

    const auto supports = source.faces();
    const auto faces = result.faces();
    for (const FaceId original : supports) {
        const auto &surface = source.face(original).surface;
        FK_CHECK(std::any_of(faces.begin(), faces.end(), [&](FaceId f) {
            return result.face(f).surface == surface;
        }));
    }
    int walls = 0;
    for (const FaceId f : faces) {
        const auto &surface = result.face(f).surface;
        if (std::any_of(supports.begin(), supports.end(), [&](FaceId original) {
                return source.face(original).surface == surface;
            })) continue;
        ++walls;
        FK_CHECK(surface->type() == SurfaceType::BSpline);
    }
    FK_CHECK(walls > 100);
    FK_CHECK(result.edges().size() > source.edges().size());
    FK_CHECK(checkBody(readBodyBinary(writeBodyBinary(result)), {true, true}).empty());
}
