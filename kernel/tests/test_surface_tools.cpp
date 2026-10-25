#include "surface_tools_fixture.h"
#include "fk_test.h"
#include "fk_surface_algo.h"
#include "fk_body_check.h"
#include "fk_classify.h"
#include "fk_mass.h"
#include "fk_sheet.h"
#include "fk_sew.h"
#include "fk_primitives.h"
#include "fk_tessellate.h"
#include "fk_extrude.h"
using namespace ForgeCad::Kernel;
namespace {
void valid(const Body &b) {
    for (const auto &issue : checkBody(b, {true, true}))
        fktest::reportFailure(__FILE__, __LINE__, issue.message);
    FK_CHECK(tessellate(b, {}).failedFaces == 0);
}
double area(const Body &b) {
    double value = 0;
    for (const auto f : b.faces())
        value += faceArea(b, f);
    return value;
}
} // namespace
FK_TEST(SurfaceNetworkCrossingFamilies) {
    std::vector<PathSegment> rows, columns;
    const auto at = [](double x, double y) { return Vec3(x, y, .1 * x * y); };
    for (double y : {0., 5., 10.})
        rows.push_back(surface_fixture::line(at(-2, y), at(12, y)));
    for (double x : {0., 5., 10.})
        columns.push_back(surface_fixture::line(at(x, -2), at(x, 12)));
    FaceAliases names;
    const auto body = curveNetworkSheet(rows, columns, 1e-6, &names);
    valid(body);
    FK_CHECK(body.faces().size() == 4);
    FK_CHECK(body.shells().size() == 1);
    FK_CHECK(names.size() == 4);
    for (const auto &family : {rows, columns})
        for (const auto &piece : family)
            for (int k = 0; k <= 10; ++k) {
                const auto p = piece.curve->point(piece.range.lo + piece.range.length() * (2. + k) / 14);
                double best = 1e300;
                for (const auto f : body.faces())
                    best = std::min(best, projectPoint(*body.face(f).surface, p).distance);
                FK_CHECK(best < 1e-6);
            }
    rows[1] = surface_fixture::line(at(12, 5), at(-2, 5));
    columns[2] = surface_fixture::line(at(10, 12), at(10, -2));
    valid(curveNetworkSheet(rows, columns));
    std::swap(columns[0], columns[1]);
    FK_CHECK_THROWS(curveNetworkSheet(rows, columns));
    std::swap(columns[0], columns[1]);
    columns[1] = columns[0];
    FK_CHECK_THROWS(curveNetworkSheet(rows, columns));
    columns[1] = surface_fixture::line(Vec3(5, -2, 4), Vec3(5, 12, 4));
    FK_CHECK_THROWS(curveNetworkSheet(rows, columns));
    FK_CHECK_THROWS(curveNetworkSheet({rows.front()}, columns));
    FK_CHECK_THROWS(curveNetworkSheet(rows, columns, 0));
}
FK_TEST(SurfaceExtendUntilPlaneAndCurvedFace) {
    const auto source = surface_fixture::patch();
    valid(source);
    const auto edge = surface_fixture::edgeNear(source, Vec3(10, 5, 0));
    const auto wall = [](double x, double slant = 0) {
        return makePlaneSheet(Frame3(Vec3(x, 5, 0), Vec3(1, slant, 0), Vec3(0, 0, 1)), 100);
    };
    BodyHistory history;
    const auto result = extendSheetTo(source, edge, wall(14), 10, false, 1e-6, &history);
    valid(result);
    FK_CHECK_NEAR(area(result), 140, 1e-5);
    const auto slanted = extendSheetTo(source, edge, wall(15, .2), 10);
    valid(slanted);
    FK_CHECK_NEAR(area(slanted), 150, 1e-5);
    FK_CHECK_THROWS(extendSheetTo(source, edge, wall(14), 2));
    FK_CHECK_THROWS(extendSheetTo(source, edge, wall(5), 10));
    const auto shortWall = makePlaneSheet(Frame3(Vec3(14, 5, 0), Vec3(1, 0, 0), Vec3(0, 0, 1)), 2);
    FK_CHECK_THROWS(extendSheetTo(source, edge, shortWall, 10));
    const auto cylinder = makeCylinder(Frame3(Vec3(30, 5, -3), Vec3(0, 0, 1), Vec3(1, 0, 0)), 16, 6);
    std::vector<FaceId> faces;
    for (const auto f : cylinder.faces())
        if (cylinder.face(f).surface->type() == SurfaceType::Cylinder)
            faces.push_back(f);
    const auto curved = extendSheetTo(source, edge, facesAsSheet(cylinder, faces), 10);
    valid(curved);
    FK_CHECK(area(curved) > 140 && area(curved) < 145);
    // Due pareti dividono il prolungamento in tre parti: deve restare
    // quella che contiene la base, indipendentemente dall'ordine dei target.
    const Body nearWall = wall(14), farWall = wall(18);
    for (bool reverse : {false, true}) {
        const auto target = sewSheets(reverse ? std::vector<const Body *>{&farWall, &nearWall}
                                             : std::vector<const Body *>{&nearWall, &farWall}, 1e-6, false);
        BodyHistory declared;
        const auto multiple = extendSheetTo(source, edge, target.body, 10, false, 1e-6, &declared);
        valid(multiple);
        FK_CHECK_NEAR(area(multiple), 140, 1e-5);
        FK_CHECK(declared.operandCount() == 1);
        for (const auto f : multiple.faces()) {
            for (const auto &origin : declared.origins(EntityKind::Face, f.index))
                FK_CHECK(origin.operand == 0 && source.contains(FaceId(origin.index)));
        }
    }
    FK_CHECK_NEAR(area(source), 100, 1e-8);
}
FK_TEST(SurfaceExtendUntilFacetedTarget) {
    const auto source = surface_fixture::patch();
    const std::vector<Vec2> points{{14, -1}, {16, 5}, {14, 11}};
    std::vector<ProfileSegment> segments;
    for (std::size_t i = 1; i < points.size(); ++i)
        segments.push_back({std::make_shared<Line<2>>(points[i - 1], points[i] - points[i - 1]),
                            {0, distance(points[i - 1], points[i])}});
    const auto profile = buildProfile(segments, 1e-9);
    const auto target = makeSheetExtrusion(Frame3(Vec3(0, 0, -2), Vec3(0, 0, 1), Vec3(1, 0, 0)), profile.chains, 4);
    const auto edge = surface_fixture::edgeNear(source, Vec3(10, 5, 0));
    for (const auto f : target.faces())
        FK_CHECK_THROWS(extendSheetTo(source, edge, facesAsSheet(target, {f}), 100));
    for (double maximum : {10., 100.}) {
        const auto result = extendSheetTo(source, edge, target, maximum);
        valid(result);
        FK_CHECK_NEAR(area(result), 455. / 3., 1e-5);
        for (double y : {0., 2.5, 5., 7.5, 10.}) {
            const double x = 16 - std::fabs(y - 5) / 3;
            FK_CHECK(classifyPointOnFace(result, result.faces().front(), Vec3(x - .01, y, 0), 1e-6) != PointLocation::Outside);
            FK_CHECK(classifyPointOnFace(result, result.faces().front(), Vec3(x + .01, y, 0), 1e-6) == PointLocation::Outside);
        }
    }
}
FK_TEST(SurfaceExtendUntilSplineExtrusion) {
    // Due estrusioni di spline con direzioni ortogonali. Il bordo lontano
    // generato da extendSheet e' una TransformedCurve della spline iniziale.
    const auto sheet = [](const Frame3 &frame, const std::vector<Vec2> &poles, double length) {
        const auto curve = std::make_shared<BSplineCurve<2>>(2, std::vector<double>{0, 0, 0, 1, 1, 1}, poles);
        const auto profile = buildProfile({{curve, curve->domain()}}, 1e-9);
        return makeSheetExtrusion(frame, profile.chains, length);
    };
    const auto source = sheet(Frame3(Vec3(), Vec3(1, 0, 0), Vec3(0, 1, 0)),
                              {Vec2(0, -1), Vec2(.4, 0), Vec2(0, 1)}, 1);
    const auto target = sheet(Frame3(Vec3(0, -2, 0), Vec3(0, 1, 0), Vec3(1, 0, 0)),
                              {Vec2(-2, -2), Vec2(-3, 0), Vec2(-2, 2)}, 4);
    const auto edge = surface_fixture::edgeNear(source, Vec3(0, .2, 0));
    for (double maximum : {5., 100.}) {
        const auto result = extendSheetTo(source, edge, target, maximum);
        valid(result);
        FK_CHECK(result.faces().size() == 1);
        FK_CHECK(area(result) > area(source));
        const auto face = result.faces().front();
        for (const auto e : source.edges()) {
            const auto &boundary = source.edge(e);
            for (int i = 0; i <= 8; ++i)
                FK_CHECK(classifyPointOnFace(result, face, boundary.curve->point(boundary.range.lo + boundary.range.length() * i / 8.), 1e-6)
                         != PointLocation::Outside);
        }
        bool reached = false;
        for (const auto e : result.edges()) {
            const auto &boundary = result.edge(e);
            const auto p = boundary.curve->point((boundary.range.lo + boundary.range.hi) / 2);
            reached = reached || projectPoint(*target.face(target.faces().front()).surface, p).distance < 1e-6;
        }
        FK_CHECK(reached);
    }
    FK_CHECK_THROWS(extendSheetTo(source, edge, target, .5));
}
FK_TEST(SurfacePolesPreserveTrimsAndTopology) {
    for (bool hole : {false, true}) {
        const auto source = surface_fixture::patch(hole);
        valid(source);
        BodyHistory history;
        const auto changed = editSheetPoles(source, {{1, 1, Vec3(0, 0, 2), 1.5}}, 1e-6, &history);
        valid(changed);
        const auto f = changed.faces().front();
        const auto *s = dynamic_cast<const BSplineSurface *>(changed.face(f).surface.get());
        FK_CHECK(s && s->uDegree() == 3 && s->vDegree() == 3 && s->uPoleCount() == 4);
        FK_CHECK_NEAR(s->point(.5, .5).z(), (3. * 9 / 64) / (1. + .5 * 9 / 64), 1e-10);
        FK_CHECK(changed.edges() == source.edges());
        FK_CHECK(changed.vertices() == source.vertices());
        FK_CHECK(changed.face(f).loops.size() == source.face(source.faces().front()).loops.size());
        FK_CHECK(classifyPointOnFace(changed, f, s->point(.5, .5), 1e-6) == (hole ? PointLocation::Outside : PointLocation::Inside));
        FK_CHECK_NEAR(source.face(source.faces().front()).surface->point(.5, .5).z(), 0, 0);
        const auto boundary = editSheetPoles(source, {{0, 1, Vec3(0, 0, 1), 1}}, 1e-6);
        valid(boundary);
        FK_CHECK(boundary.vertex(boundary.vertices().front()).point.z() == 0);
        FK_CHECK_THROWS(editSheetPoles(source, {{1, 1, Vec3(), 0}}));
        FK_CHECK_THROWS(editSheetPoles(source, {{1, 1, Vec3(), 1}, {1, 1, Vec3(0, 0, 1), 1}}));
        FK_CHECK_THROWS(editSheetPoles(source, {{4, 0, Vec3(), 1}}));
    }
    const auto source = surface_fixture::patch();
    std::vector<SurfacePoleChange> collapse;
    const auto *s = dynamic_cast<const BSplineSurface *>(source.face(source.faces().front()).surface.get());
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            collapse.push_back({i, j, -s->pole(i, j), 1});
    FK_CHECK_THROWS(editSheetPoles(source, collapse));
    FK_CHECK_THROWS(editSheetPoles(makePlaneSheet(Frame3(), 10), {{0, 0, Vec3(0, 0, 1), 1}}));
}
