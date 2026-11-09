#include "fk_body_check.h"
#include "fk_body_io.h"
#include "fk_helix.h"
#include "fk_primitives.h"
#include "fk_revolve.h"
#include "fk_sew.h"
#include "fk_sheet.h"
#include "fk_surface_algo.h"
#include "fk_surface_poles_constraints.h"
#include "fk_surface_poles_conversion.h"
#include "fk_test.h"
#include "surface_tools_fixture.h"
using namespace ForgeCad::Kernel;
FK_TEST(SurfacePolesExactAnalyticConversion) {
    const std::vector<Body> shapes{makePlaneSheet(Frame3(), 10),   makeBox(Frame3(), 10, 15, 20),
                                   makeCylinder(Frame3(), 10, 15), makeCone(Frame3(), 10, 5, 15),
                                   makeSphere(Frame3(), 10),       makeTorus(Frame3(), 15, 5)};
    for (const auto &source : shapes) {
        const auto before = writeBodyBinary(source);
        const auto converted = prepareSurfacePoleBody(source, source.faces(), 1e-6);
        FK_CHECK(checkBody(converted, {true, true}).empty());
        FK_CHECK(converted.faces() == source.faces() && converted.edges() == source.edges() &&
                 converted.vertices() == source.vertices());
        for (auto edge : source.edges())
            FK_CHECK(converted.edge(edge).curve == source.edge(edge).curve);
        for (auto f : converted.faces()) {
            const auto s = std::dynamic_pointer_cast<const BSplineSurface>(converted.face(f).surface);
            FK_CHECK(bool(s) && s->uPoleCount() >= 3 && s->vPoleCount() >= 3);
            const auto u = s->uDomain(), v = s->vDomain();
            for (int i = 1; i < 8; ++i)
                for (int j = 1; j < 8; ++j) {
                    const auto p = s->point(u.lo + u.length() * i / 8., v.lo + v.length() * j / 8.);
                    FK_CHECK(projectPoint(*source.face(f).surface, p).distance < 1e-8);
                }
        }
        FK_CHECK(writeBodyBinary(source) == before);
    }
}
FK_TEST(SurfacePolesAnalyticEditsHolesAndClosure) {
    const auto prism =
        makePrism(Frame3(), {{-10, -10}, {10, -10}, {10, 10}, {-10, 10}}, {{{-2, -2}, {-2, 2}, {2, 2}, {2, -2}}}, 5);
    const auto sheet = facesAsSheet(prism, {prism.faces().front()});
    auto plane = prepareSurfacePoleBody(sheet, sheet.faces(), 1e-6);
    FK_CHECK(plane.face(plane.faces().front()).loops.size() == 2);
    auto changed = editBodyFacesPoles(plane, {{plane.faces().front(), {{1, 1, {0, 0, .2}, 1}}, {}}});
    FK_CHECK(checkBody(changed, {true, true}).empty());
    FK_CHECK(changed.face(changed.faces().front()).loops.size() == 2);
    for (auto source : {makeCylinder(Frame3(), 10, 15), makeCone(Frame3(), 10, 5, 15), makeSphere(Frame3(), 10),
                        makeTorus(Frame3(), 15, 5)}) {
        auto body = prepareSurfacePoleBody(source, source.faces(), 1e-6);
        for (auto f : source.faces()) {
            const auto type = source.face(f).surface->type();
            if (type == SurfaceType::Plane)
                continue;
            auto s = std::dynamic_pointer_cast<const BSplineSurface>(body.face(f).surface);
            changed = editBodyFacesPoles(body, {{f, {{2, 2, {.01, 0, 0}, 1}}, {}}});
            FK_CHECK(checkBody(changed, {true, true}).empty());
            const std::vector<SurfaceFacePoleChange> seam{{f, {{0, 2, {.01, 0, 0}, 1}}, {}}};
            FK_CHECK_THROWS(editBodyFacesPoles(body, seam));
            SurfacePoleConstraints constraints(body, {{f, {}, {}}}, 1, 1e-6);
            changed = editBodyFacesPoles(body, constraints.propagate(seam));
            FK_CHECK(checkBody(changed, {true, true}).empty());
            auto modified = std::dynamic_pointer_cast<const BSplineSurface>(changed.face(f).surface);
            FK_CHECK_NEAR(modified->pole(0, 2).x(), modified->pole(s->uPoleCount() - 1, 2).x(), 1e-10);
        }
    }
}

FK_TEST(SurfacePolesMixedAnalyticAndNurbsTangency) {
    auto mixed = surface_fixture::strip();
    const auto faces = mixed.faces();
    mixed.face(faces[0]).surface = std::make_shared<Plane>(Frame3());
    for (auto loop : mixed.face(faces[0]).loops)
        for (auto fin : mixed.loopFins(loop))
            mixed.fin(fin).pcurve.reset();
    SurfacePoleConstraints legacy(mixed, {{faces[1], {}, {}}}, 2, 1e-6);
    FK_CHECK(legacy.report().protectedJoints == 1);
    FK_CHECK(
        checkBody(editBodyFacesPoles(mixed, legacy.propagate({{faces[1], {{2, 1, {0, 0, .1}, 1}}, {}}})), {true, true})
            .empty());
    auto converted = prepareSurfacePoleBody(mixed, faces, 1e-6);
    SurfacePoleConstraints tangent(converted, {{faces[0], {}, {}}}, 2, 1e-6);
    SurfacePoleContinuityReport info;
    auto changes = tangent.propagate({{faces[0], {{3, 1, {0, 0, .2}, 1}}, {}}}, &info);
    const auto result = editBodyFacesPoles(converted, changes);
    FK_CHECK(info.followers == 3);
    const auto report = checkSurfacePoleContinuity(converted, result, faces, 1e-6);
    FK_CHECK(report.gap < 1e-9 && report.angle < 1e-9);
    FK_CHECK(checkBody(result, {true, true}).empty());
}
FK_TEST(SurfacePolesCurvedAnalyticTrimmedHoles) {
    for (const SurfacePtr &surface : std::vector<SurfacePtr>{
             std::make_shared<CylindricalSurface>(Frame3(), 10), std::make_shared<ConicalSurface>(Frame3(), .4, 10),
             std::make_shared<SphericalSurface>(Frame3(), 10), std::make_shared<ToroidalSurface>(Frame3(), 15, 5)}) {
        std::vector<Vec3> vertices;
        std::vector<Body::BuildEdge> edges;
        Body::BuildFace face;
        face.surface = surface;
        for (int hole = 0; hole < 2; ++hole) {
            const double a = hole ? .7 : .3, b = hole ? 1.2 : 1.7, c = hole ? .3 : .1, d = hole ? .6 : .8;
            const int i = vertices.size(), e = edges.size();
            for (const auto uv : std::vector<Vec2>{{a, c}, {b, c}, {b, d}, {a, d}})
                vertices.push_back(surface->point(uv.x(), uv.y()));
            edges.push_back({i, i + 1, surface->vIso(c), {a, b}, 1e-9});
            edges.push_back({i + 1, i + 2, surface->uIso(b), {c, d}, 1e-9});
            edges.push_back({i + 3, i + 2, surface->vIso(d), {a, b}, 1e-9});
            edges.push_back({i, i + 3, surface->uIso(a), {c, d}, 1e-9});
            if (!hole)
                face.loops.push_back(
                    {{e, true, {}, 0}, {e + 1, true, {}, 0}, {e + 2, false, {}, 0}, {e + 3, false, {}, 0}});
            else
                face.loops.push_back(
                    {{e + 3, true, {}, 0}, {e + 2, true, {}, 0}, {e + 1, false, {}, 0}, {e, false, {}, 0}});
        }
        const auto source = Body::buildSheet(vertices, edges, {face});
        const auto converted = prepareSurfacePoleBody(source, source.faces(), 1e-6);
        FK_CHECK(checkBody(converted, {true, true}).empty());
        const auto changed =
            editBodyFacesPoles(converted, {{converted.faces().front(), {{1, 1, {.001, 0, 0}, 1}}, {}}});
        FK_CHECK(changed.face(changed.faces().front()).loops.size() == 2);
        FK_CHECK(checkBody(changed, {true, true}).empty());
    }
}

// Un giro oltre la cucitura: i trim della filettatura di Hook attraversano u=0.
FK_TEST(SurfacePolesPeriodicHelicalTrims) {
    HelixSpec spec; spec.radius = 10; spec.pitch = 3; spec.turns = 1.2; spec.startAngle = -.2;
    const HelixCurve lower(spec);
    auto bottom = std::make_shared<BSplineCurve<3>>(helixBSpline(lower, 1e-10));
    spec.frame = Frame3({0, 0, 1},{0,0,1},{1,0,0});
    auto top = std::make_shared<BSplineCurve<3>>(helixBSpline(HelixCurve(spec), 1e-10));
    const auto r = lower.domain();
    std::vector<Vec3> points{bottom->point(r.lo), bottom->point(r.hi), top->point(r.hi), top->point(r.lo)};
    const auto line = [](Vec2 a, Vec2 b, Interval r) {
        return std::make_shared<BSplineCurve<2>>(1, std::vector<double>{r.lo,r.lo,r.hi,r.hi}, std::vector<Vec2>{a,b});
    };
    const double end = spec.startAngle + r.hi;
    Body::BuildFace face; face.surface = std::make_shared<CylindricalSurface>(Frame3(), 10);
    face.loops = {{{0,true,line({-.2,0},{end,3*spec.turns},r),1e-9},
                   {1,true,line({end,3*spec.turns},{end,3*spec.turns+1},{0,1}),1e-9},
                   {2,false,line({-.2,1},{end,3*spec.turns+1},r),1e-9},
                   {3,false,line({-.2,0},{-.2,1},{0,1}),1e-9}}};
    const auto source = Body::buildSheet(points, {{0,1,bottom,r,1e-9},
        {1,2,std::make_shared<Line<3>>(points[1],Vec3(0,0,1)),{0,1},1e-9}, {3,2,top,r,1e-9},
        {0,3,std::make_shared<Line<3>>(points[0],Vec3(0,0,1)),{0,1},1e-9}}, {face});
    const auto converted = prepareSurfacePoleBody(source,source.faces(),1e-6);
    FK_CHECK(checkBody(converted,{true,true}).empty());
    const auto s = std::dynamic_pointer_cast<const BSplineSurface>(converted.face(converted.faces()[0]).surface);
    FK_CHECK(s->isUPeriodic() && !s->isVPeriodic());
    for (double u : {-.1,.1,6.4}) {
        Vec3 a[9],b[9]; s->evaluate(u,.5,2,a);s->evaluate(u+kTwoPi,.5,2,b);
        for(int i:{0,1,2,3,4,6}) FK_CHECK(distance(a[i],b[i])<1e-10);
    }
    FK_CHECK(distance(s->uIsoCurve(-.1).point(.5),s->point(-.1,.5))<1e-10);
    FK_CHECK(s->uBreakpoints({-.1,6.4}).size()>5);
    FK_CHECK(s->uBreakpoints({1e18,1e18}).size()==1);
    const auto bytes = writeBodyBinary(converted);
    FK_CHECK(static_cast<unsigned char>(bytes[4])==3);
    const auto reread = readBodyBinary(bytes);
    FK_CHECK(writeBodyBinary(reread)==bytes);
    FK_CHECK(checkBody(reread,{true,true}).empty());
    const auto moved = transformBody(converted,Transform3::translation({1,2,3}));
    const auto restored = readBodyBinary(writeBodyBinary(moved));
    const auto rs = std::dynamic_pointer_cast<const BSplineSurface>(restored.face(restored.faces()[0]).surface);
    FK_CHECK(rs->isUPeriodic() && distance(rs->point(-.1,.5),s->point(-.1,.5)+Vec3(1,2,3))<1e-10);
    auto pool = makeBodyReadPool();
    const auto pooled = readBodyBinary(bytes,*pool);
    auto unwrapped=converted;
    std::vector<Vec3> poles; std::vector<double> weights;
    for(int i=0;i<s->uPoleCount();++i)for(int j=0;j<s->vPoleCount();++j){poles.push_back(s->pole(i,j));weights.push_back(s->weight(i,j));}
    unwrapped.face(unwrapped.faces()[0]).surface=std::make_shared<BSplineSurface>(s->uDegree(),s->vDegree(),s->uKnots(),s->vKnots(),s->uPoleCount(),s->vPoleCount(),poles,weights);
    const auto legacyBytes=writeBodyBinary(unwrapped);
    FK_CHECK(static_cast<unsigned char>(legacyBytes[4])==2);
    const auto old=readBodyBinary(legacyBytes,*pool);
    FK_CHECK(!old.face(old.faces()[0]).surface->isUPeriodic());
    FK_CHECK(old.face(old.faces()[0]).surface!=pooled.face(pooled.faces()[0]).surface);
}
FK_TEST(SurfacePolesSphericalCapAtConstantLatitude) {
    const auto sphere=std::make_shared<SphericalSurface>(Frame3(),10);
    const double latitude=.202149;
    std::vector<Vec3> vertices;std::vector<Body::BuildEdge> edges;Body::BuildFace face;face.surface=sphere;
    std::vector<Body::BuildFin> loop;
    for(int i=0;i<4;++i)vertices.push_back(sphere->point(.35+i*kTwoPi/4,latitude));
    for(int i=0;i<4;++i){Interval r{.35+i*kTwoPi/4,.35+(i+1)*kTwoPi/4};
        edges.push_back({i,(i+1)%4,sphere->vIso(latitude),r,1e-9});
        loop.push_back({i,true,std::make_shared<BSplineCurve<2>>(1,std::vector<double>{r.lo,r.lo,r.hi,r.hi},std::vector<Vec2>{{r.lo,latitude},{r.hi,latitude}}),0});}
    face.loops={loop};const auto source=Body::buildSheet(vertices,edges,{face});
    const auto converted=prepareSurfacePoleBody(source,source.faces(),1e-6);
    const auto s=std::dynamic_pointer_cast<const BSplineSurface>(converted.face(converted.faces()[0]).surface);
    FK_CHECK(s->vDomain().length()>1 && s->isUPeriodic());
    FK_CHECK(checkBody(converted,{true,true}).empty());
    for(const auto f:converted.fins()){const auto &use=converted.fin(f);const auto &edge=converted.edge(use.edge);
        FK_CHECK(pcurveDeviation(*s,*edge.curve,*use.pcurve,edge.range,128)<1e-6);}
}
