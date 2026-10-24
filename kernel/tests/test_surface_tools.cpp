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
    FK_CHECK_NEAR(area(source), 100, 1e-8);
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
