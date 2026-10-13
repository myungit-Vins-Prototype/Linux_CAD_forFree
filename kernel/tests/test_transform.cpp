#include <cmath>

#include "fk_blend.h"
#include "fk_bspline_surface.h"
#include "fk_pcurve.h"
#include "fk_body_check.h"
#include "fk_boolean.h"
#include "fk_extrude.h"
#include "fk_mass.h"
#include "fk_primitives.h"
#include "fk_revolve.h"
#include "fk_tessellate.h"
#include "fk_test_profiles.h"
#include "fk_transform.h"

using namespace fktest;

namespace {

// Il body scalato: valido, volume s^3, area s^2, baricentro scalato attorno al centro.
void checkScaled(const Body &body, const Vec3 &center, double s) {
    Body scaled;
    try {
        scaled = scaleBody(body, center, s);
    } catch (const std::exception &error) {
        reportFailure(__FILE__, __LINE__, error.what());
        return;
    }
    FK_CHECK(checkBody(scaled).empty());
    const MassProperties before = massProperties(body), after = massProperties(scaled);
    FK_CHECK_NEAR(after.volume, s * s * s * before.volume, 1e-9 * after.volume);
    FK_CHECK_NEAR(after.area, s * s * before.area, 1e-9 * after.area);
    FK_CHECK(distance(after.centroid, center + s * (before.centroid - center)) < 1e-9 * (1.0 + s * norm(before.centroid)));
    FK_CHECK(scaled.faces().size() == body.faces().size() && scaled.edges().size() == body.edges().size());
    TessellationOptions options;
    options.deflection = 0.01 * s;
    FK_CHECK(tessellate(scaled, options).failedFaces == 0);
}

}

FK_TEST(TransformScaleBodies) {
    const Vec3 center(1, -2, 0.5);
    // Estrusione con archi e segmenti, cilindro, sfera, cono, toro.
    const Body rounded = makeExtrusion(Frame3(), buildProfile(roundedRectangle(Vec2(0, 0), 6, 4, 1), 1e-9).regions.front(), 2.0);
    for (double s : {0.25, 3.0}) {
        checkScaled(rounded, center, s);
        checkScaled(makeCylinder(Frame3(Vec3(1, 2, 3), Vec3(0, 1, 1), Vec3(1, 0, 0)), 1.5, 4.0), center, s);
        checkScaled(makeSphere(Frame3(Vec3(2, 0, 1), Vec3(0, 0, 1), Vec3(1, 0, 0)), 2.0), center, s);
        checkScaled(makeCone(Frame3(), 2.0, 0.5, 3.0), center, s);
        checkScaled(makeTorus(Frame3(), 3.0, 1.0), center, s);
    }
    // Una booleana tra cilindri (curve del marching con SP-curve approssimate) e il suo raccordo.
    const Body main = makeCylinder(Frame3(Vec3(-5, 0, 0), Vec3(1, 0, 0), Vec3(0, 1, 0)), 2.0, 10.0);
    const Body tee = booleanOperation(main, makeCylinder(Frame3(Vec3(0, 0, 0), Vec3(0, 0, 1), Vec3(1, 0, 0)), 1.0, 4.0), BooleanOperation::Unite);
    checkScaled(tee, center, 2.5);
    // Una rotazione con traslazione lascia invariati volume e area.
    const Transform3 motion = Transform3::translation(Vec3(3, 1, -2)) * Transform3::rotation(Vec3(0, 0, 0), normalized(Vec3(1, 1, 0)), 0.7);
    const Body moved = transformBody(tee, motion);
    FK_CHECK(checkBody(moved).empty());
    FK_CHECK_NEAR(massProperties(moved).volume, massProperties(tee).volume, 1e-9 * massProperties(tee).volume);
    // Scala nulla o simmetrie: errore.
    FK_CHECK_THROWS(scaleBody(rounded, center, 0.0));
    FK_CHECK_THROWS(scaleBody(rounded, center, -1.0));
}

#include "fk_loft.h"

namespace {

// L'immagine speculare: valida, stesso volume e area, baricentro riflesso.
void checkMirrored(const std::string &name, const Body &body, const Vec3 &point, const Vec3 &normal) {
    Body mirrored;
    try {
        mirrored = mirrorBody(body, point, normal);
    } catch (const std::exception &error) {
        reportFailure(__FILE__, __LINE__, name + ": " + error.what());
        return;
    }
    for (const CheckIssue &issue : checkBody(mirrored)) reportFailure(__FILE__, __LINE__, name + ": " + describe(issue.code) + " " + issue.message);
    const MassProperties before = massProperties(body, 1e-10), after = massProperties(mirrored, 1e-10);
    if (std::fabs(after.volume - before.volume) > 1e-8 * before.volume)
        reportFailure(__FILE__, __LINE__, name + ": volume " + std::to_string(after.volume) + " invece di " + std::to_string(before.volume));
    FK_CHECK_NEAR(after.area, before.area, 1e-8 * before.area);
    const Vec3 n = normalized(normal);
    const Vec3 expected = before.centroid - 2.0 * dot(before.centroid - point, n) * n;
    FK_CHECK(distance(after.centroid, expected) < 1e-7 * (1.0 + norm(expected)));
    TessellationOptions options;
    options.deflection = 0.01;
    FK_CHECK(tessellate(mirrored, options).failedFaces == 0);
}

}

FK_TEST(TransformMirrorBodies) {
    const Vec3 point(1, -2, 0.5), normal = normalized(Vec3(1, 0.3, -0.2));
    const Body rounded = makeExtrusion(Frame3(), buildProfile(roundedRectangle(Vec2(0, 0), 6, 4, 1), 1e-9).regions.front(), 2.0);
    checkMirrored("estrusione", rounded, point, normal);
    checkMirrored("spline", makeExtrusion(Frame3(), buildProfile({closedSpline(Vec2(0, 0), 3.0, true)}, 1e-9).regions.front(), 1.5), point, normal);
    checkMirrored("cilindro", makeCylinder(Frame3(Vec3(1, 2, 3), Vec3(0, 1, 1), Vec3(1, 0, 0)), 1.5, 4.0), point, normal);
    checkMirrored("sfera", makeSphere(Frame3(Vec3(2, 0, 1), Vec3(0, 0, 1), Vec3(1, 0, 0)), 2.0), point, normal);
    checkMirrored("cono", makeCone(Frame3(), 2.0, 0.5, 3.0), point, normal);
    checkMirrored("toro", makeTorus(Frame3(), 3.0, 1.0), point, Vec3(0, 0, 1));
    const Body main = makeCylinder(Frame3(Vec3(-5, 0, 0), Vec3(1, 0, 0), Vec3(0, 1, 0)), 2.0, 10.0);
    const Body tee = booleanOperation(main, makeCylinder(Frame3(Vec3(0, 0, 0), Vec3(0, 0, 1), Vec3(1, 0, 0)), 1.0, 4.0), BooleanOperation::Unite);
    checkMirrored("innesto a T", tee, point, normal);
    LoftSection a, b;
    a.loop.segments = {arcSegment(Vec2(0, 0), 2.0, 0.0, kTwoPi)};
    b.frame = Frame3(Vec3(0, 0, 3), Vec3(0, 0, 1), Vec3(1, 0, 0));
    b.loop.segments = {lineSegment(Vec2(1, 0), Vec2(1, 1)), lineSegment(Vec2(1, 1), Vec2(-1, 1)), lineSegment(Vec2(-1, 1), Vec2(-1, -1)),
                       lineSegment(Vec2(-1, -1), Vec2(1, -1)), lineSegment(Vec2(1, -1), Vec2(1, 0))};
    checkMirrored("loft", loftSolid({a, b}, false), point, normal);
    // Il corpo e la sua immagine si uniscono (lo stesso piano di simmetria taglia il blocco a meta').
    const Body block = makeBox(Frame3(Vec3(-1, 0, 0), Vec3(0, 0, 1), Vec3(1, 0, 0)), 3.0, 2.0, 1.0);
    const Body both = booleanOperation(block, mirrorBody(block, Vec3(0, 0, 0), Vec3(1, 0, 0)), BooleanOperation::Unite);
    FK_CHECK(checkBody(both).empty());
    FK_CHECK_NEAR(massProperties(both).volume, 4.0 * 2.0 * 1.0, 1e-9);
}

FK_TEST(TransformMirrorRetainsSplineBoundaryParameters) {
    // Pezza curva, anche razionale: parametri UV invariati dalla simmetria.
    for(bool rational:{false,true}) {
        std::vector<Vec3> poles{{0,0,0},{0,1,0},{0,2,0},{1,0,0},{1,1,1},{1,2,0},{2,0,0},{2,1,0},{2,2,0}};
        std::vector<double> weights;if(rational)weights={1,1,1,1,2,1,1,1,1};
        auto surface=std::make_shared<BSplineSurface>(2,2,std::vector<double>{0,0,0,1,1,1},std::vector<double>{0,0,0,1,1,1},3,3,poles,weights);
        const std::vector<Vec2> corners{{0,0},{1,0},{1,1},{0,1}};std::vector<Vec3> points;
        for(Vec2 uv:corners)points.push_back(surface->point(uv.x(),uv.y()));
        std::vector<Body::BuildEdge> edges;std::vector<Body::BuildFin> fins;
        for(int i=0;i<4;++i){auto uv=std::make_shared<BSplineCurve<2>>(3,std::vector<double>{0,0,0,0,1,1,1,1},std::vector<Vec2>{corners[i],corners[i]+(corners[(i+1)%4]-corners[i])/3.,corners[i]+2.*(corners[(i+1)%4]-corners[i])/3.,corners[(i+1)%4]});
            CurvePtr<3> curve=i==0?surface->vIso(0):i==1?surface->uIso(1):i==2?surface->vIso(1):surface->uIso(0);
            const bool sense=i<2;edges.push_back({sense?i:(i+1)%4,sense?(i+1)%4:i,curve,{0,1}});
            // Il parametro dell'edge segue l'isoparametrica, non il loop.
            if(!sense)uv=std::make_shared<BSplineCurve<2>>(3,std::vector<double>{0,0,0,0,1,1,1,1},std::vector<Vec2>{corners[(i+1)%4],corners[(i+1)%4]+(corners[i]-corners[(i+1)%4])/3.,corners[(i+1)%4]+2.*(corners[i]-corners[(i+1)%4])/3.,corners[i]});
            fins.push_back({i,sense,uv,1e-10});}
        const Body source=Body::buildSheet(points,edges,{{surface,true,{fins}}});
        for(double scale:{1.,2.}) {
            const Transform3 transform=Transform3::scaling(Vec3(),scale)*Transform3::reflection({0.3,0,0},normalized(Vec3(1,0.2,0.1)));
            const Body image=transformBody(source,transform);FK_CHECK(checkBody(image).empty());
            for(FinId id:image.fins()){auto &fin=image.fin(id);bool retained=false;
                for(FinId original:source.fins())retained=retained||fin.pcurve==source.fin(original).pcurve;
                FK_CHECK(retained);FK_CHECK_NEAR(fin.pcurveTolerance,scale*1e-10,1e-15);
                FK_CHECK(pcurveDeviation(*image.face(image.finFace(id)).surface,*image.edge(fin.edge).curve,*fin.pcurve,image.edge(fin.edge).range,16)<1e-10);}
        }
    }
}

FK_TEST(TransformMirrorRecalculatesScaledLineParameters) {
    auto surface=std::make_shared<BSplineSurface>(1,1,std::vector<double>{0,0,1,1},std::vector<double>{0,0,1,1},2,2,std::vector<Vec3>{{0,0,0},{0,1,0},{1,0,0},{1,1,0}});
    const std::vector<Vec3> points{{0,0,0},{1,0,0},{1,1,0},{0,1,0}};
    std::vector<Body::BuildEdge> edges;std::vector<Body::BuildFin> fins;
    for(int i=0;i<4;++i){const Vec3 a=points[i],b=points[(i+1)%4];
        auto uv=std::make_shared<BSplineCurve<2>>(1,std::vector<double>{0,0,1,1},std::vector<Vec2>{{a.x(),a.y()},{b.x(),b.y()}});
        edges.push_back({i,(i+1)%4,std::make_shared<Line<3>>(a,b-a),{0,1}});fins.push_back({i,true,uv});}
    const Body source=Body::buildSheet(points,edges,{{surface,true,{fins}}});
    const Body image=transformBody(source,Transform3::scaling(Vec3(),2)*Transform3::reflection(Vec3(),Vec3(1,0,0)));
    FK_CHECK(checkBody(image).empty());
    for(FinId id:image.fins()){const auto &fin=image.fin(id);const auto &edge=image.edge(fin.edge);
        FK_CHECK_NEAR(edge.range.length(),2.,1e-12);
        for(FinId old:source.fins())FK_CHECK(fin.pcurve!=source.fin(old).pcurve);
        FK_CHECK(pcurveDeviation(*image.face(image.finFace(id)).surface,*edge.curve,*fin.pcurve,edge.range,16)<1e-10);}
}
