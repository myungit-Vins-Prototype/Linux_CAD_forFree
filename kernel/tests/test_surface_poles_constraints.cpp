#include "fk_revolve.h"
#include "fk_surface_poles_conversion.h"
#include "fk_body_check.h"
#include "fk_body_io.h"
#include "fk_surface_poles_constraints.h"
#include "fk_surface_refit.h"
#include "fk_test.h"
#include "surface_tools_fixture.h"
using namespace ForgeCad::Kernel;
namespace {
Body quad(BSplineSurfacePtr surface) {
    std::vector<Vec3> vertices{surface->point(0, 0), surface->point(1, 0), surface->point(1, 1), surface->point(0, 1)};
    std::vector<Body::BuildEdge> edges{{0, 1, std::make_shared<BSplineCurve<3>>(surface->vIsoCurve(0)), {0, 1}, 1e-9},
                                       {1, 2, std::make_shared<BSplineCurve<3>>(surface->uIsoCurve(1)), {0, 1}, 1e-9},
                                       {3, 2, std::make_shared<BSplineCurve<3>>(surface->vIsoCurve(1)), {0, 1}, 1e-9},
                                       {0, 3, std::make_shared<BSplineCurve<3>>(surface->uIsoCurve(0)), {0, 1}, 1e-9}};
    const auto line = [](Vec2 a, Vec2 b) {
        return std::make_shared<BSplineCurve<2>>(1, std::vector<double>{0, 0, 1, 1}, std::vector<Vec2>{a, b});
    };
    Body::BuildFace face;
    face.surface = std::move(surface);
    face.loops = {{{0, true, line({0, 0}, {1, 0}), 1e-9},
                   {1, true, line({1, 0}, {1, 1}), 1e-9},
                   {2, false, line({0, 1}, {1, 1}), 1e-9},
                   {3, false, line({0, 0}, {0, 1}), 1e-9}}};
    return Body::buildSheet(vertices, edges, {face});
}
BSplineSurfacePtr plane(bool second, bool swapped = false, bool weighted = false, bool sharp = false) {
    std::vector<Vec3> points;
    std::vector<double> weights;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            double x = 10. * (swapped ? j : i) / 3, y = 10. * (swapped ? 3 - i : j) / 3;
            points.push_back(second ? Vec3(10 + x, y, sharp ? x : 0) : Vec3(x, y, 0));
            weights.push_back(weighted ? (swapped ? (j == 1 ? 2. : 1.) : (i == 1 ? 2. : 1.)) : 1.);
        }
    return std::make_shared<const BSplineSurface>(3, 3, std::vector<double>{0, 0, 0, 0, 1, 1, 1, 1},
                                                  std::vector<double>{0, 0, 0, 0, 1, 1, 1, 1}, 4, 4, points, weights);
}
} // namespace
FK_TEST(SurfacePolesAutomaticConnectionAndTangency) {
    const auto source = surface_fixture::strip();
    const auto before = writeBodyBinary(source);
    const auto faces = source.faces();
    const std::vector<SurfaceFacePoleChange> driver{{faces[0], {{3, 1, {0, 0, .2}, 1}}, {}}};
    SurfacePoleConstraints connected(source, driver, 1, 1e-6), tangent(source, driver, 2, 1e-6);
    SurfacePoleContinuityReport info;
    const auto c0 = editBodyFacesPoles(source, connected.propagate(driver, &info));
    FK_CHECK(info.followers == 1);
    const auto c1 = editBodyFacesPoles(source, tangent.propagate(driver, &info));
    FK_CHECK(info.followers == 3);
    const auto report0 = checkSurfacePoleContinuity(source, c0, faces, 1e-6),
               report1 = checkSurfacePoleContinuity(source, c1, faces, 1e-6);
    FK_CHECK(report0.gap < 1e-10 && report0.angle > .01);
    FK_CHECK(report1.gap < 1e-10 && report1.angle < 1e-10);
    const auto neighbor = std::dynamic_pointer_cast<const BSplineSurface>(c1.face(faces[1]).surface);
    FK_CHECK_NEAR(neighbor->pole(0, 1).z(), .2, 1e-12);
    FK_CHECK_NEAR(neighbor->pole(1, 1).z(), .2, 1e-12);
    FK_CHECK(c1.face(faces[2]).surface == source.face(faces[2]).surface);
    FK_CHECK(checkBody(c1, {true, true}).empty());
    FK_CHECK(writeBodyBinary(source) == before);
    auto conflict = driver;
    conflict.push_back({faces[1], {{0, 1, {0, 0, .3}, 1}}, {}});
    FK_CHECK_THROWS(tangent.propagate(conflict));
    const std::vector<std::array<int, 3>> active{{faces[1].index, 0, 1}};
    const auto following = tangent.propagate(conflict, &info, &active);
    const auto moved = editBodyFacesPoles(source, following);
    FK_CHECK_NEAR(std::dynamic_pointer_cast<const BSplineSurface>(moved.face(faces[0]).surface)->pole(3, 1).z(), .3,
                  1e-12);
    FK_CHECK(checkSurfacePoleContinuity(source, moved, faces, 1e-6).angle < 1e-10);
    auto zero = conflict;
    zero[1].changes[0].delta = {};
    const auto anchored = editBodyFacesPoles(source, tangent.propagate(zero, nullptr, &active));
    FK_CHECK_NEAR(std::dynamic_pointer_cast<const BSplineSurface>(anchored.face(faces[0]).surface)->pole(3, 1).z(), 0,
                  1e-12);
    auto badWeight = driver;
    badWeight[0].changes[0].weightFactor = 2;
    FK_CHECK_THROWS(tangent.propagate(badWeight));
    const auto interior = tangent.propagate({{faces[0], {{2, 1, {0, 0, .2}, 1}}, {}}});
    const auto tilted = editBodyFacesPoles(source, interior);
    FK_CHECK(checkSurfacePoleContinuity(source, tilted, faces, 1e-6).angle < 1e-10);
}
FK_TEST(SurfacePolesContinuitySwappedReversedRationalAndSharp) {
    for (bool weighted : {false, true})
        for (bool sharp : {false, true}) {
            const auto a = quad(plane(false, false, weighted)), b = quad(plane(true, true, weighted, sharp));
            const auto source = sewSheets({&a, &b}, 1e-6, false).body;
            const auto faces = source.faces();
            FK_CHECK(faces.size() == 2);
            SurfacePoleConstraints constraints(source, {{faces[0], {}, {}}}, 2, 1e-6);
            const auto info = constraints.report();
            FK_CHECK(info.connected == 1 && info.protectedJoints == 0);
            FK_CHECK(info.sharp == (sharp ? 1 : 0));
            const std::vector<SurfaceFacePoleChange> driver{{faces[0], {{3, 1, {0, 0, .2}, 1}}, {}}};
            auto result = editBodyFacesPoles(source, constraints.propagate(driver));
            FK_CHECK(checkBody(result, {true, true}).empty());
            auto neighbor = std::dynamic_pointer_cast<const BSplineSurface>(result.face(faces[1]).surface);
            FK_CHECK_NEAR(neighbor->pole(2, 0).z(), .2, 1e-11);
            const auto report = checkSurfacePoleContinuity(source, result, faces, 1e-6);
            FK_CHECK(report.gap < 1e-9);
            if (!sharp)
                FK_CHECK(report.angle < 1e-9);
        }
}
FK_TEST(SurfacePolesContinuityRefitAndProtection) {
    const auto source = surface_fixture::strip();
    const auto faces = source.faces();
    std::vector<SurfaceFacePoleChange> bases;
    for (auto face : faces) {
        auto s = std::dynamic_pointer_cast<const BSplineSurface>(source.face(face).surface);
        bases.push_back({face, {}, refitSurfacePoles(*s, 6, 8).surface});
    }
    SurfacePoleConstraints fit(source, bases, 2, 1e-6);
    FK_CHECK(fit.refitDeviation() < 1e-10);
    bases[0].changes = {{5, 3, {0, 0, .2}, 1}};
    auto result = editBodyFacesPoles(source, fit.propagate(bases));
    FK_CHECK(checkSurfacePoleContinuity(source, result, faces, 1e-6).angle < 1e-9);
    bases[1].replacement =
        refitSurfacePoles(*std::dynamic_pointer_cast<const BSplineSurface>(source.face(faces[1]).surface), 6, 9)
            .surface;
    SurfacePoleConstraints incompatible(source, bases, 2, 1e-6);
    FK_CHECK(incompatible.report().protectedJoints > 0);
    FK_CHECK_THROWS(incompatible.propagate(bases));
    const auto s = bases[1].replacement;
    std::vector<Vec3> moved;
    for (int u = 0; u < s->uPoleCount(); ++u)
        for (int v = 0; v < s->vPoleCount(); ++v)
            moved.push_back(s->pole(u, v) + (u == 0 && v == 3 ? Vec3(0, 0, .001) : Vec3()));
    bases[1].replacement = std::make_shared<const BSplineSurface>(s->uDegree(), s->vDegree(), s->uKnots(), s->vKnots(),
                                                                  s->uPoleCount(), s->vPoleCount(), moved);
    FK_CHECK_THROWS(SurfacePoleConstraints(source, bases, 2, 1e-6));
}
FK_TEST(SurfacePolesTrimProtectionLeavesInteriorFree) {
    const auto dense=refitSurfacePoles(*plane(false),24,32).surface;
    auto a=quad(dense);
    std::vector<Vec3> points;
    for(int i=0;i<dense->uPoleCount();++i)for(int j=0;j<dense->vPoleCount();++j){auto p=dense->pole(i,j);p[0]*=2;points.push_back(p);}
    a.face(a.faces()[0]).surface=std::make_shared<BSplineSurface>(dense->uDegree(),dense->vDegree(),dense->uKnots(),dense->vKnots(),24,32,points);
    for(auto f:a.fins()){
        auto &use=a.fin(f);const auto range=a.edge(use.edge).range;
        auto p=use.pcurve->point(range.lo),q=use.pcurve->point(range.hi);p[0]*=.5;q[0]*=.5;
        use.pcurve=std::make_shared<BSplineCurve<2>>(1,std::vector<double>{range.lo,range.lo,range.hi,range.hi},std::vector<Vec2>{p,q});
    }
    const auto b=quad(plane(true));
    const auto source=sewSheets({&a,&b},1e-6,false).body;
    const auto faces=source.faces();
    SurfacePoleConstraints constraints(source,{{faces[0],{},{}}},2,1e-6);
    FK_CHECK(constraints.report().protectedJoints==1);
    FK_CHECK(constraints.propagate({}).empty());
    const auto changes=constraints.propagate({{faces[0],{{4,16,{0,0,.2},1}}, {}}});
    const auto result=editBodyFacesPoles(source,changes);
    FK_CHECK(checkBody(result,{true,true}).empty());
    FK_CHECK(checkSurfacePoleContinuity(source,result,faces,1e-6).gap<1e-10);
    const auto s=std::dynamic_pointer_cast<const BSplineSurface>(result.face(faces[0]).surface);
    FK_CHECK_NEAR(s->pole(4,16).z(),.2,1e-12);
    FK_CHECK_THROWS(constraints.propagate({{faces[0],{{12,16,{0,0,.2},1}}, {}}}));
}
FK_TEST(SurfacePolesNeutralTolerantConnections) {
    const auto a=quad(plane(false));
    const auto b=transformBody(quad(plane(true)),Transform3::translation({0,0,2e-8}));
    const auto source=sewSheets({&a,&b},1e-6,false).body;
    const auto faces=source.faces();
    SurfacePoleConstraints constraints(source,{{faces[0],{},{}}},2,1e-6);
    FK_CHECK(constraints.propagate({}).empty());
    const auto result=editBodyFacesPoles(source,constraints.propagate({{faces[0],{{3,1,{0,0,.2},1}},{}}}));
    const auto report=checkSurfacePoleContinuity(source,result,faces,1e-6);
    FK_CHECK(report.gap<1e-6 && report.angle<1e-9);
    FK_CHECK_NEAR(std::dynamic_pointer_cast<const BSplineSurface>(result.face(faces[1]).surface)->pole(0,1).z(),.2+2e-8,1e-12);
}

FK_TEST(SurfacePolesSphereInternalG1AndG2) {
    const auto source=prepareSurfacePoleBody(makeSphere(Frame3(),10),{FaceId(0)},1e-6);
    const auto face=source.faces().front();
    const auto neutral=std::dynamic_pointer_cast<const BSplineSurface>(source.face(face).surface);
    FK_CHECK(neutral->uPoleCount()==9&&neutral->vPoleCount()==5);
    const auto before=writeBodyBinary(source);
    const std::vector<SurfaceFacePoleChange> driver{{face,{{2,3,{.4,.1,.2},1}}, {}}};
    const auto free=editBodyFacesPoles(source,driver);
    const auto freeReport=checkSurfacePoleContinuity(source,free,{face},1e-6);
    FK_CHECK(freeReport.internalTangentError>1e-4);
    for(int mode:{3,4}) {
        SurfacePoleConstraints constraints(source,{{face,{}, {}}},mode,1e-6);
        SurfacePoleContinuityReport info;
        const auto edits=constraints.propagate(driver,&info);
        FK_CHECK(info.internal>=6&&info.followers>0);
        const auto changed=editBodyFacesPoles(source,edits);
        const auto report=checkSurfacePoleContinuity(source,changed,{face},1e-6);
        FK_CHECK(report.internalTangentError<1e-9);
        if(mode==4)FK_CHECK(report.curvatureError<1e-9);
        // Verifica geometrica indipendente dal solver ai due lati dell'equatore:
        // normali e curvatura normale della sezione meridiana.
        const auto &s=*changed.face(face).surface;
        for(double u:{.3,1.,2.,3.7,5.1}) {
            Vec3 a[9],b[9];s.evaluate(u,-1e-8,2,a);s.evaluate(u,1e-8,2,b);
            const auto na=normalized(cross(a[3],a[1])),nb=normalized(cross(b[3],b[1]));
            FK_CHECK(norm(cross(na,nb))<1e-7);
            if(mode==4)FK_CHECK_NEAR(dot(a[2],na)/squaredNorm(a[1]),dot(b[2],nb)/squaredNorm(b[1]),1e-7);
        }
        FK_CHECK(checkBody(changed,{true,true}).empty());
        // Rigenerazione esatta dei valori risolti e pesi sul giunto protetti.
        FK_CHECK(writeBodyBinary(editBodyFacesPoles(source,constraints.propagate(edits)))==writeBodyBinary(changed));
        auto bad=driver;bad[0].changes[0].weightFactor=1.1;FK_CHECK_THROWS(constraints.propagate(bad));
    }
    FK_CHECK(writeBodyBinary(source)==before);
}

FK_TEST(SurfacePolesG2AcrossFacesAndRigidPoses) {
    for(bool weighted:{false,true}) {
        const auto first=quad(plane(false)),second=quad(plane(true,true,weighted));
        const auto source=sewSheets({&first,&second},1e-6,false).body;
        const auto faces=source.faces();
        SurfacePoleConstraints constraints(source,{{faces[0],{}, {}}},4,1e-6);
        const std::vector<SurfaceFacePoleChange> driver{{faces[0],{{3,1,{0,0,.2},1}}, {}}};
        const auto result=editBodyFacesPoles(source,constraints.propagate(driver));
        const auto report=checkSurfacePoleContinuity(source,result,faces,1e-6);
        FK_CHECK(report.curvature==1&&report.curvatureError<1e-10&&report.angle<1e-9);
        // Curvatura normale trasversale misurata dai due supporti, con
        // parametri scambiati e orientazione inversa della seconda faccia.
        Vec3 a[9],b[9];result.face(faces[0]).surface->evaluate(1,.4,2,a);result.face(faces[1]).surface->evaluate(.6,0,2,b);
        const auto na=normalized(cross(a[3],a[1])),nb=normalized(cross(b[3],b[1]));
        FK_CHECK_NEAR(dot(a[6],na)/squaredNorm(a[3]),dot(b[2],nb)/squaredNorm(b[1]),1e-10);
        FK_CHECK(checkBody(result,{true,true}).empty());
    }
    // Grande traslazione: le relazioni non dipendono dall'origine del mondo.
    const auto sphere=prepareSurfacePoleBody(makeSphere(Frame3({1e7,-2e7,3e7},{0,0,1},{1,0,0}),10),{FaceId(0)},1e-6);
    SurfacePoleConstraints constraints(sphere,{{FaceId(0),{}, {}}},4,1e-6);
    const auto result=editBodyFacesPoles(sphere,constraints.propagate({{FaceId(0),{{2,3,{.4,.1,.2},1}}, {}}}));
    const auto report=checkSurfacePoleContinuity(sphere,result,sphere.faces(),1e-6);
    FK_CHECK(report.curvatureError<1e-8&&report.internalTangentError<1e-8);
}
