#include "surface_tools_fixture.h"
#include "fk_test.h"
#include "fk_surface_refit.h"
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

FK_TEST(SurfacePolesRefitControlCounts) {
    const auto source=surface_fixture::patch(true);
    const auto original=std::dynamic_pointer_cast<const BSplineSurface>(source.face(source.faces().front()).surface);
    const auto fitted=refitSurfacePoles(*original,6,9);
    FK_CHECK(fitted.surface->uPoleCount()==6 && fitted.surface->vPoleCount()==9);
    FK_CHECK(fitted.sampledDeviation<1e-10);
    BodyHistory history;
    const auto rebuilt=editSheetPoles(source,{},1e-6,&history,fitted.surface);
    valid(rebuilt);
    FK_CHECK(rebuilt.edges()==source.edges() && rebuilt.vertices()==source.vertices());
    FK_CHECK(rebuilt.face(rebuilt.faces().front()).loops.size()==2);
    const auto edited=editSheetPoles(source,{{2,4,Vec3(0,0,.2),1}},1e-6,&history,fitted.surface);
    valid(edited);
    FK_CHECK(original->uPoleCount()==4 && original->vPoleCount()==4);
    FK_CHECK_THROWS(refitSurfacePoles(*original,3,8));
    FK_CHECK_THROWS(refitSurfacePoles(*original,8,surfaceRefitMaxPoles+1));
    // Razionale curva: lo scarto scende aumentando i controlli; verifica su
    // una griglia indipendente da campioni e nodi usati dal ricostruttore.
    std::vector<Vec3> points;
    std::vector<double> weights(16,1.);
    for(int u=0;u<4;++u)for(int v=0;v<4;++v)points.emplace_back(u,v,u==1&&v==1?3.:0.);
    weights[5]=1.8;
    const BSplineSurface rational(3,3,{0,0,0,0,1,1,1,1},{0,0,0,0,1,1,1,1},4,4,points,weights);
    const auto coarse=refitSurfacePoles(rational,4,4),fine=refitSurfacePoles(rational,12,12);
    FK_CHECK(fine.sampledDeviation<coarse.sampledDeviation*.1);
    const auto automatic=refitSurfacePolesWithinTolerance({std::make_shared<const BSplineSurface>(rational)},4,4,.001);
    FK_CHECK(automatic.sampledDeviation<=.001);
    FK_CHECK(automatic.u>4 || automatic.v>4);
    FK_CHECK(automatic.faces.size()==1);
    FK_CHECK(automatic.u<=surfaceRefitMaxPoles && automatic.v<=surfaceRefitMaxPoles);
    FK_CHECK_THROWS(refitSurfacePolesWithinTolerance({},4,4,.001));
    FK_CHECK_THROWS(refitSurfacePolesWithinTolerance({original},4,4,0));
    const auto wide=refitSurfacePoles(*original,6,96);
    FK_CHECK(wide.sampledDeviation<1e-10 && wide.surface->vPoleCount()==96);
    double independent=0;
    for(int i=0;i<53;++i)for(int j=0;j<47;++j) {
        const double u=(i+.371)/53,v=(j+.619)/47;
        independent=std::max(independent,distance(rational.point(u,v),fine.surface->point(u,v)));
    }
    FK_CHECK(independent<=fine.sampledDeviation*1.1+1e-10);
}

FK_TEST(SurfacePolesMultipleConnectedFaces) {
    const auto source=surface_fixture::strip();valid(source);
    const auto faces=source.faces();FK_CHECK(faces.size()==3);
    FK_CHECK(dynamic_cast<const BSplineSurface *>(source.face(faces[0]).surface.get()));
    const auto fixed=source.face(faces[2]).surface;
    BodyHistory history;
    const auto edited=editBodyFacesPoles(source,{{faces[0],{{1,1,{0,0,.2},1}},{}},{faces[1],{{2,2,{0,0,.3},1}}, {}}},1e-6,&history);
    valid(edited);FK_CHECK(edited.faces()==faces && edited.edges()==source.edges());
    FK_CHECK(edited.face(faces[2]).surface==fixed);
    FK_CHECK(edited.face(faces[0]).surface->point(.5,.5).z()>0);
    FK_CHECK(edited.face(faces[1]).surface->point(.5,.5).z()>0);
    FK_CHECK(source.face(faces[0]).surface->point(.5,.5).z()==0);
    const auto joined=editBodyFacesPoles(source,{{faces[0],{{3,1,{0,0,.2},1}},{}},{faces[1],{{0,1,{0,0,.2},1}}, {}}});
    valid(joined);
    FK_CHECK_THROWS(editBodyFacesPoles(source,{{faces[0],{{3,1,{0,0,.2},1}}, {}}}));
    FK_CHECK_THROWS(editBodyFacesPoles(source,{{faces[0],{{1,1,{0,0,.2},1}},{}},{faces[0],{{2,2,{0,0,.3},1}}, {}}}));
    std::vector<SurfaceFacePoleChange> refitted;
    for (int i=0;i<2;++i) {
        const auto original=std::dynamic_pointer_cast<const BSplineSurface>(source.face(faces[i]).surface);
        refitted.push_back({faces[i],{},refitSurfacePoles(*original,6,8).surface});
    }
    const auto reduced=editBodyFacesPoles(source,refitted);valid(reduced);
    FK_CHECK(reduced.face(faces[2]).surface==fixed);
    for(const auto face:faces) FK_CHECK(!history.origins(EntityKind::Face,face.index).empty());
}

FK_TEST(SurfacePolesMultipleFacesOfSolid) {
    auto source=makeBox(Frame3(),10,10,10);
    FK_CHECK(computePCurves(source)==0);
    const auto faces=source.faces();
    for (int k=0;k<2;++k) {
        const auto face=faces[k];const auto plane=source.face(face).surface;
        double u0=1e30,u1=-1e30,v0=1e30,v1=-1e30;
        for(const auto loop:source.face(face).loops) for(const auto fin:source.loopFins(loop)) {
            const auto &e=source.edge(source.fin(fin).edge);
            for(const double t:{e.range.lo,e.range.hi}) {
                const auto q=source.fin(fin).pcurve->point(t);
                u0=std::min(u0,q.x());u1=std::max(u1,q.x());v0=std::min(v0,q.y());v1=std::max(v1,q.y());
            }
        }
        std::vector<Vec3> points;
        for(int i=0;i<4;++i)for(int j=0;j<4;++j)points.push_back(plane->point(u0+(u1-u0)*i/3,v0+(v1-v0)*j/3));
        source.face(face).surface=std::make_shared<BSplineSurface>(3,3,std::vector<double>{u0,u0,u0,u0,u1,u1,u1,u1},
            std::vector<double>{v0,v0,v0,v0,v1,v1,v1,v1},4,4,points);
    }
    valid(source);
    const auto edited=editBodyFacesPoles(source,{{faces[0],{{1,1,{0,0,.1},1}},{}},{faces[1],{{1,1,{0,0,.1},1}}, {}}});
    valid(edited);FK_CHECK(!edited.isSheet());FK_CHECK(edited.faces()==faces);
    for(int k=2;k<6;++k)FK_CHECK(edited.face(faces[k]).surface==source.face(faces[k]).surface);
}

FK_TEST(SurfacePolesPreserveDeclaredVertexTolerance) {
    auto source=surface_fixture::patch();
    const auto edge=source.edges().front();const auto fin=source.edge(edge).forward;
    const auto range=source.edge(edge).range;
    const auto uv=source.fin(fin).pcurve;
    const auto first=uv->point(range.lo),last=uv->point(range.hi);
    source.fin(fin).pcurve=std::make_shared<BSplineCurve<2>>(1,std::vector<double>{range.lo,range.lo,range.hi,range.hi},
        std::vector<Vec2>{first+Vec2(2e-7,0),last});
    const auto vertex=source.edgeStart(edge);
    source.vertex(vertex).tolerance=3e-6;source.edge(edge).tolerance=2.1e-6;
    valid(source);
    const auto edited=editSheetPoles(source,{{1,1,{0,0,.1},1}});
    valid(edited);
    FK_CHECK(edited.vertex(vertex).tolerance==source.vertex(vertex).tolerance);
    FK_CHECK(edited.edge(edge).tolerance==source.edge(edge).tolerance);
}
