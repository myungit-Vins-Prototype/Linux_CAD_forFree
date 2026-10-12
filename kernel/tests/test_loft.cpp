#include <BRepGProp.hxx>
#include <BRepOffsetAPI_ThruSections.hxx>
#include <GProp_GProps.hxx>

#include <cmath>
#include <limits>

#include "fk_body_check.h"
#include "fk_curve_algo.h"
#include "fk_surface_algo.h"
#include "fk_bspline_surface.h"
#include "fk_loft.h"
#include "fk_nurbs.h"
#include "fk_mass.h"
#include "fk_offset.h"
#include "fk_tessellate.h"
#include "fk_test_profiles.h"

using namespace fktest;

namespace {

LoftSection circleAt(double z, double r, const Vec3 &shift = Vec3()) {
    LoftSection s;
    s.frame = Frame3(Vec3(0, 0, z) + shift, Vec3(0, 0, 1), Vec3(1, 0, 0));
    s.loop.segments = {arcSegment(Vec2(0, 0), r, 0.0, kTwoPi)};
    return s;
}

LoftSection squareAt(double z, double a, bool clockwise = false) {
    LoftSection s;
    s.frame = Frame3(Vec3(0, 0, z), Vec3(0, 0, 1), Vec3(1, 0, 0));
    std::vector<Vec2> c{Vec2(a / 2, 0), Vec2(a / 2, a / 2), Vec2(-a / 2, a / 2), Vec2(-a / 2, -a / 2), Vec2(a / 2, -a / 2)};
    // Parte a meta' del lato destro (come il cerchio ad angolo 0).
    std::vector<ProfileSegment> segments;
    for (std::size_t i = 0; i < c.size(); ++i) segments.push_back(lineSegment(c[i], c[(i + 1) % c.size()]));
    s.loop.segments = segments;
    if (clockwise) s.loop = reversed(s.loop);
    return s;
}

MassProperties checkedSolid(const Body &body) {
    for (const CheckIssue &issue : checkBody(body)) reportFailure(__FILE__, __LINE__, describe(issue.code) + ": " + issue.message);
    FK_CHECK(!body.isSheet());
    TessellationOptions options;
    options.deflection = 0.01;
    FK_CHECK(tessellate(body, options).failedFaces == 0);
    return massProperties(body, 1e-12);
}

double occtLoftVolume(const std::vector<LoftSection> &sections, bool ruled) {
    BRepOffsetAPI_ThruSections loft(true, ruled, 1e-7);
    for (const LoftSection &s : sections) loft.AddWire(occtWire(s.loop, s.frame));
    loft.Build();
    GProp_GProps props;
    BRepGProp::VolumeProperties(loft.Shape(), props, 1e-9);
    return std::fabs(props.Mass());
}

}

FK_TEST(LoftRuledFrustums) {
    // Tronco di cono (cerchi coassiali) e di piramide: volumi esatti e OCCT.
    const double R = 2.0, r = 1.0, h = 3.0;
    const std::vector<LoftSection> cone{circleAt(0.0, R), circleAt(h, r)};
    const double coneVolume = kPi * h / 3.0 * (R * R + R * r + r * r);
    FK_CHECK_NEAR(checkedSolid(loftSolid(cone, true)).volume, coneVolume, 1e-10 * coneVolume);
    FK_CHECK_NEAR(occtLoftVolume(cone, true), coneVolume, 1e-6 * coneVolume);
    // Il verso dei loop non conta (uno orario).
    const std::vector<LoftSection> pyramid{squareAt(0.0, 4.0), squareAt(h, 2.0, true)};
    const double pyramidVolume = h / 3.0 * (16.0 + 4.0 + 8.0);
    const MassProperties m = checkedSolid(loftSolid(pyramid, true));
    FK_CHECK_NEAR(m.volume, pyramidVolume, 1e-10 * pyramidVolume);
    FK_CHECK_NEAR(occtLoftVolume(pyramid, true), pyramidVolume, 1e-9 * pyramidVolume);
    // Tre sezioni rigate: due tronchi.
    const std::vector<LoftSection> three{circleAt(0.0, R), circleAt(h, r), circleAt(2 * h, R)};
    FK_CHECK_NEAR(checkedSolid(loftSolid(three, true)).volume, 2.0 * coneVolume, 1e-10 * coneVolume);
}

FK_TEST(LoftSmoothThroughCircles) {
    // Tre cerchi coassiali: raggio R(z) = 2 - z + z^2 / 4 (quadratica), V = 112 pi / 15.
    const std::vector<LoftSection> sections{circleAt(0.0, 2.0), circleAt(2.0, 1.0), circleAt(4.0, 2.0)};
    const MassProperties m = checkedSolid(loftSolid(sections, false));
    FK_CHECK_NEAR(m.volume, 112.0 * kPi / 15.0, 1e-10 * m.volume);
    FK_CHECK(std::hypot(m.centroid.x(), m.centroid.y()) < 1e-10);
    FK_CHECK_NEAR(m.centroid.z(), 2.0, 1e-10);
    // Due sezioni lisce = rigate.
    FK_CHECK_NEAR(checkedSolid(loftSolid({circleAt(0.0, 2.0), circleAt(3.0, 1.0)}, false)).volume, kPi * (4.0 + 2.0 + 1.0), 1e-10 * 22.0);
}

// Le campate razionali di un'ellisse ruotata hanno lunghezze diverse.
// Usarle come nuovi intervalli parametrici introduceva spigoli interni
// nel loft e trasformava due facce laterali in sei durante l'offset.
FK_TEST(LoftEllipsePreservesSmoothOffsetFaces) {
    auto middle = circleAt(10.0, 1.0);
    const auto ellipse = std::make_shared<Ellipse<2>>(Vec2(), Vec2(1,0), Vec2(0,1), 5.0, 2.5);
    middle.loop.segments = {{ellipse, {0.17, 0.17 + kTwoPi}}};
    const Body body = loftSolid({circleAt(0.0, 2.5), middle, circleAt(20.0, 4.0)}, false);
    checkedSolid(body);
    std::vector<FaceId> sides;
    for (FaceId f : body.faces()) {
        const auto &surface = *body.face(f).surface;
        if (surface.type() != SurfaceType::BSpline) continue;
        sides.push_back(f);
        const auto &spline = static_cast<const BSplineSurface &>(surface);
        const auto patches = spline.cachedBezierPatches();
        const auto us = spline.uBreakpoints(spline.uDomain());
        const auto vs = spline.vBreakpoints(spline.vDomain());
        const std::size_t nv = vs.size() - 1;
        for (std::size_t i = 1; i + 1 < us.size(); ++i)
            for (std::size_t j = 0; j < nv; ++j)
                for (double fraction : {0.25, 0.5, 0.75}) {
                    const double v = vs[j] + fraction * (vs[j+1] - vs[j]);
                    FK_CHECK_NEAR(distance((*patches)[(i-1)*nv+j].normal(us[i],v),
                                           (*patches)[i*nv+j].normal(us[i],v)), 0.0, 1e-8);
                }
    }
    FK_CHECK(sides.size() == 2);
    for (double d : {-0.5, 1.0}) {
        const auto result = offsetFaces(body, sides, d, 1e-7, true);
        FK_CHECK(result.body.faces().size() == sides.size());
        FK_CHECK(checkBody(result.body).empty());
        FK_CHECK(result.notes.empty());

    }
}

FK_TEST(LoftCircleToSquare) {
    // Sezioni diverse: il cerchio si divide nei punti del quadrato. Confronto con OCCT.
    for (bool ruled : {true, false}) {
        const std::vector<LoftSection> sections = ruled ? std::vector<LoftSection>{circleAt(0.0, 1.5), squareAt(3.0, 2.0)}
                                                        : std::vector<LoftSection>{circleAt(0.0, 1.5), squareAt(2.0, 2.0), circleAt(4.0, 1.0, Vec3(0.5, 0, 0))};
        const Body body = loftSolid(sections, ruled);
        const MassProperties m = checkedSolid(body);
        FK_CHECK(m.volume > 0.0);
        // OCCT sceglie la stessa corrispondenza ma interpola diversamente tra le sezioni.
        const double reference = occtLoftVolume(sections, ruled);
        FK_CHECK_NEAR(m.volume, reference, 0.02 * reference);
    }
}

FK_TEST(LoftGuideCrossesEverySection) {
    const std::vector<LoftSection> sections{circleAt(0.0, 2.0), circleAt(3.0, 1.0)};
    LoftOptions options;
    options.ruled = true;
    const Vec3 a(2, 0, 0), b(1, 0, 3), direction = normalized(b - a);
    options.guides.push_back({{std::make_shared<Line<3>>(a, direction), {0.0, distance(a, b)}}});
    const Body guided = loftSolid(sections, options);
    FK_CHECK_NEAR(checkedSolid(guided).volume, kPi * (4.0 + 2.0 + 1.0), 1e-10 * 22.0);

    LoftOptions invalid = options;
    invalid.guides.front().front() = {std::make_shared<Line<3>>(Vec3(4, 0, 0), Vec3(0, 0, 1)), {0.0, 3.0}};
    bool failed = false;
    try {
        (void)loftSolid(sections, invalid);
    } catch (const std::domain_error &) {
        failed = true;
    }
    FK_CHECK(failed);
}

FK_TEST(LoftGuideInfluenceAndEndContinuity) {
    const std::vector<LoftSection> sections{circleAt(0.0, 5.0), circleAt(1.0, 1.0), circleAt(4.0, 1.1)};
    const Vec3 p[3] = {Vec3(5, 0, 0), Vec3(1, 0, 1), Vec3(1.1, 0, 4)};
    LoftOptions free, guided;
    for (int i = 0; i < 2; ++i) {
        const Vec3 direction = normalized(p[i + 1] - p[i]);
        free.guides.resize(1);
        free.guides.front().push_back({std::make_shared<Line<3>>(p[i], direction), {0.0, distance(p[i], p[i + 1])}});
    }
    guided = free;
    free.guideInfluence = 0.0;
    guided.guideInfluence = 1.0;
    const double freeVolume = checkedSolid(loftSolid(sections, free)).volume;
    const double guidedVolume = checkedSolid(loftSolid(sections, guided)).volume;
    FK_CHECK(std::fabs(guidedVolume - freeVolume) > 1e-3 * freeVolume);

    guided.guideContinuity = 1;
    const double tangentOnlyVolume = checkedSolid(loftSolid(sections, guided)).volume;
    guided.guideContinuity = 2;
    const double curvatureVolume = checkedSolid(loftSolid(sections, guided)).volume;
    FK_CHECK(std::fabs(tangentOnlyVolume - curvatureVolume) > 1e-8 * tangentOnlyVolume);

    guided.startContinuity = 1;
    guided.endContinuity = 2;
    guided.startInfluence = 0.7;
    guided.endInfluence = 0.8;
    FK_CHECK(checkedSolid(loftSolid(sections, guided)).volume > 0.0);
}

FK_TEST(LoftGuideG0DoesNotEnableDerivativeConstraints) {
    const std::vector<LoftSection> sections{circleAt(0.0, 1.0), circleAt(2.0, 2.0), circleAt(4.0, 1.0)};
    LoftOptions options;
    options.guideContinuity = 0;
    options.guideInfluence = 1.0;
    options.guides.push_back({{std::make_shared<BSplineCurve<3>>(2, std::vector<double>{0, 0, 0, 1, 1, 1},
                                                                  std::vector<Vec3>{{1, 0, 0}, {3, 0, 2}, {1, 0, 4}}),
                               {0.0, 1.0}}});
    const Body body = loftSolid(sections, options);
    checkedSolid(body);
    for (FaceId face : body.faces()) {
        if (body.face(face).surface->type() != SurfaceType::BSpline) continue;
        const auto &surface = static_cast<const BSplineSurface &>(*body.face(face).surface);
        FK_CHECK(surface.vDegree() == 2);
    }
}

FK_TEST(LoftTangencyAppliesToEveryGuide) {
    const std::vector<LoftSection> sections{circleAt(0.0, 1.0), circleAt(2.0, 2.0), circleAt(4.0, 1.0)};
    LoftOptions options;
    options.guideContinuity = 1;
    options.guideInfluence = 1.0;
    const auto right = std::make_shared<BSplineCurve<3>>(2, std::vector<double>{0, 0, 0, 1, 1, 1},
                                                          std::vector<Vec3>{{1, 0, 0}, {3, 0, 2}, {1, 0, 4}});
    // La seconda guida cambia posizione relativa sul perimetro: a meta' loft
    // incontra il cerchio a 120 gradi, mentre sulle estremita' e' a 180.
    const auto left = std::make_shared<BSplineCurve<3>>(2, std::vector<double>{0, 0, 0, 1, 1, 1},
                                                         std::vector<Vec3>{{-1, 0, 0}, {-1, 2 * std::sqrt(3.0), 2}, {-1, 0, 4}});
    options.guides = {{{right, {0.0, 1.0}}}, {{left, {0.0, 1.0}}}};
    const Body body = loftSolid(sections, options);
    checkedSolid(body);
    for (const auto &expected : std::vector<std::pair<Vec3, Vec3>>{{Vec3(1, 0, 0), right->derivative(0.0)},
                                                                    {Vec3(-1, 0, 0), left->derivative(0.0)}}) {
        bool found = false;
        for (EdgeId edgeId : body.edges()) {
            const Edge &edge = body.edge(edgeId);
            if (distance(edge.curve->point(edge.range.lo), expected.first) > 1e-8
                || distance(edge.curve->point(edge.range.hi), expected.first + Vec3(0, 0, 4)) > 1e-8)
                continue;
            found = true;
            FK_CHECK(norm(cross(normalized(edge.curve->derivative(edge.range.lo)), normalized(expected.second))) < 1e-8);
        }
        FK_CHECK(found);
    }
}

FK_TEST(LoftOpenGuidesAndNeighborFaces) {
    LoftSection first, last;
    first.frame=Frame3(Vec3(),Vec3(0,0,1),Vec3(1,0,0));
    last.frame=Frame3(Vec3(0,10,5),Vec3(0,0,1),Vec3(1,0,0));
    first.loop.segments=last.loop.segments={lineSegment(Vec2(-5,0),Vec2(5,0))};
    const auto line=[](Vec3 a,Vec3 b)->PathSegment {
        return {std::make_shared<Line<3>>(a,b-a),{0,distance(a,b)}};
    };
    auto adjacent=std::make_shared<const Body>(ruledSurface({line({-5,-5,0},{5,-5,0})},{line({-5,0,0},{5,0,0})}));
    auto end=std::make_shared<const Body>(ruledSurface({line({-5,10,5},{5,10,5})},{line({-5,15,10},{5,15,10})}));
    auto guide=std::make_shared<BSplineCurve<3>>(3,std::vector<double>{0,0,0,0,1,1,1,1},
        std::vector<Vec3>{{0,0,0},{1,3,0},{1,7,2},{0,10,5}});
    LoftOptions options;
    options.startContinuity=options.endContinuity=1;
    options.startFaces={{adjacent,adjacent->faces().front()}};
    options.endFaces={{end,end->faces().front()}};
    options.guides={{{guide,guide->domain()}}};
    const Body body=loftSheet({first,last},options);
    FK_CHECK(checkBody(body).empty());
    bool found=false;
    for(EdgeId id:body.edges()) {
        const Edge &edge=body.edge(id);
        if(distance(edge.curve->point(edge.range.lo),Vec3())>1e-6
           ||distance(edge.curve->point(edge.range.hi),Vec3(0,10,5))>1e-6)continue;
        found=true;
        // La guida ha anche una componente lungo il profilo: il vincolo di
        // faccia non deve cancellarla quando impone il piano tangente.
        FK_CHECK(norm(cross(normalized(edge.curve->derivative(edge.range.lo)),normalized(guide->derivative(0))))<1e-8);
        FK_CHECK(norm(cross(normalized(edge.curve->derivative(edge.range.hi)),normalized(guide->derivative(1))))<1e-8);
    }
    FK_CHECK(found);
    guide=std::make_shared<BSplineCurve<3>>(3,std::vector<double>{0,0,0,0,1,1,1,1},
        std::vector<Vec3>{{0,0,0},{1,3,1},{1,7,2},{0,10,5}});
    options.guides={{{guide,guide->domain()}}};
    FK_CHECK_THROWS(loftSheet({first,last},options));
}

FK_TEST(LoftOpenGuideNearPlaneEndpoint) {
    // Estremo positivo di pochi ulp: non c'e' cambio di segno, ma la guida
    // e' gia' sul profilo entro la tolleranza. Non va persa l'intersezione.
    LoftSection first, second;
    first.frame = Frame3(Vec3(), Vec3(0,0,1), Vec3(1,0,0));
    second.frame = Frame3(Vec3(0,0,3), Vec3(0,0,1), Vec3(1,0,0));
    first.loop.segments = second.loop.segments = {lineSegment(Vec2(-1,0),Vec2(1,0))};
    auto guide = std::make_shared<BSplineCurve<3>>(3,std::vector<double>{0,0,0,0,1,1,1,1},
        std::vector<Vec3>{{0,0,1e-30},{0,0.4,1},{0,0.4,2},{0,0,3}});
    LoftOptions options;
    options.guides = {{{guide,guide->domain()}}};
    for (bool reverse : {false,true}) {
        const Body body = loftSheet(reverse ? std::vector<LoftSection>{second,first} : std::vector<LoftSection>{first,second},options);
        FK_CHECK(body.isSheet());
        FK_CHECK(checkBody(body).empty());
        TessellationOptions mesh;
        mesh.deflection=0.02;
        FK_CHECK(tessellate(body,mesh).failedFaces==0);
    }
}

FK_TEST(LoftContactAtCoincidentEndpoint) {
    // Due profili condividono un vertice: il lato longitudinale collassa,
    // ma il piano tangente resta definito come limite geometrico.
    const auto pathLine = [](Vec3 a, Vec3 b) -> PathSegment {
        return {std::make_shared<Line<3>>(a, normalized(b-a)), {0,distance(a,b)}};
    };
    auto adjacent = std::make_shared<const Body>(ruledSurface(
        {pathLine({0,-5,0},{10,-5,0})}, {pathLine({0,0,0},{10,0,0})}));
    for (bool reverse : {false,true}) {
        LoftSection first, last;
        first.frame = Frame3(Vec3(),Vec3(0,0,1),Vec3(1,0,0));
        last.frame = Frame3(Vec3(),Vec3(1,0,0),Vec3(0,1,0));
        first.loop.segments = last.loop.segments = {lineSegment({0,0},{10,0})};
        if (reverse) { first.loop = reversed(first.loop); last.loop = reversed(last.loop); }
        LoftOptions options;
        options.startContinuity = 1;
        options.startFaces = {{adjacent,adjacent->faces().front()}};
        const Body body = loftSheet({first,last},options);
        FK_CHECK(checkBody(body).empty());
        TessellationOptions mesh; mesh.deflection = 0.05;
        const auto tessellation = tessellate(body,mesh);
        FK_CHECK(tessellation.failedFaces == 0);
        FK_CHECK(!tessellation.faces.empty() && !tessellation.faces.front().triangles.empty());
        for (const auto &face : tessellation.faces)
            for (const Vec3 &normal : face.normals)
                FK_CHECK(std::fabs(normalized(normal).z()) > 1-1e-8);
    }
    // Un'intersezione interna dei profili non va scambiata per un estremo
    // collassato: la derivata trasversale nulla resta un errore.
    auto wide = std::make_shared<const Body>(ruledSurface(
        {pathLine({-5,-5,0},{5,-5,0})}, {pathLine({-5,0,0},{5,0,0})}));
    LoftSection a,b;
    a.frame = Frame3(Vec3(),Vec3(0,0,1),Vec3(1,0,0));
    b.frame = Frame3(Vec3(),Vec3(1,0,0),Vec3(0,1,0));
    a.loop.segments = b.loop.segments = {lineSegment({-5,0},{5,0})};
    LoftOptions options; options.startContinuity=1;
    options.startFaces={{wide,wide->faces().front()}};
    FK_CHECK_THROWS(loftSheet({a,b},options));
}

FK_TEST(LoftNonparallelLongitudinalParameters) {
    // Sezioni su YZ e XY: la guida piega i bordi longitudinali. La mappa
    // deve conservare il parametro del bordo anche dove la proiezione
    // numerica sulla superficie puo' scegliere un altro ramo.
    LoftSection first, last;
    first.frame = Frame3(Vec3(), Vec3(1,0,0), Vec3(0,1,0));
    last.frame = Frame3(Vec3(0,0,25), Vec3(0,0,1), Vec3(1,0,0));
    const std::vector<double> knots{0,0,0,0,1,1,1,1};
    first.loop.segments = {{std::make_shared<BSplineCurve<2>>(3, knots,
        std::vector<Vec2>{{0,0},{3,5},{7,6},{10,0}}), {0,1}}};
    last.loop.segments = {{std::make_shared<BSplineCurve<2>>(3, knots,
        std::vector<Vec2>{{5,0},{8,3},{12,4},{15,0}}), {0,1}}};
    const auto guide = std::make_shared<BSplineCurve<3>>(3, knots,
        std::vector<Vec3>{{0,5,4.125},{2,5,6},{12,2.625,17},{10,2.625,25}});
    for (int continuity : {0,1}) {
        LoftOptions options; options.guideContinuity = continuity;
        options.guides = {{{guide,{0,1}}}};
        const Body body = loftSheet({first,last}, options);
        FK_CHECK(checkBody(body).empty());
        int longitudinal = 0;
        for (FaceId id : body.faces()) {
            const auto &face = body.face(id);
            if (face.surface->type() != SurfaceType::BSpline) continue;
            for (LoopId loop : face.loops) for (FinId fid : body.loopFins(loop)) {
                const auto &fin = body.fin(fid);
                const auto &edge = body.edge(fin.edge);
                if (distance(edge.curve->point(edge.range.lo), edge.curve->point(edge.range.hi)) < 20) continue;
                ++longitudinal;
                for (int i = 0; i <= 100; ++i) {
                    const double t = i / 100.0;
                    const Vec2 uv = fin.pcurve->point(t);
                    FK_CHECK_NEAR(uv.y(), t, 1e-12);
                    FK_CHECK(std::min(std::fabs(uv.x()),std::fabs(uv.x()-1)) < 1e-12);
                    FK_CHECK(distance(face.surface->point(uv.x(),uv.y()),edge.curve->point(t)) < 1e-8);
                }
            }
        }
        FK_CHECK(longitudinal == 4);
        TessellationOptions mesh; mesh.deflection = 0.1;
        const auto tessellation = tessellate(body,mesh);
        FK_CHECK(tessellation.failedFaces == 0);
        FK_CHECK(!tessellation.faces.empty() && !tessellation.faces.front().triangles.empty());
    }
}

FK_TEST(LoftSheet) {
    // Due segmenti paralleli: rettangolo; con una catena girata al contrario si riallinea.
    LoftSection a, b;
    a.frame = Frame3(Vec3(0, 0, 0), Vec3(0, 1, 0), Vec3(1, 0, 0));
    a.loop.segments = {lineSegment(Vec2(0, 0), Vec2(2, 0))};
    b.frame = Frame3(Vec3(0, 3, 0), Vec3(0, 1, 0), Vec3(1, 0, 0));
    b.loop.segments = {lineSegment(Vec2(2, 0), Vec2(0, 0))};
    const Body sheet = loftSheet({a, b}, true);
    FK_CHECK(sheet.isSheet());
    for (const CheckIssue &issue : checkBody(sheet)) reportFailure(__FILE__, __LINE__, describe(issue.code) + ": " + issue.message);
    double area = 0.0;
    for (FaceId f : sheet.faces()) area += faceArea(sheet, f);
    FK_CHECK_NEAR(area, 6.0, 1e-10);
}

FK_TEST(LoftPreservesDistinctNearbyKnots) {
    // These two internal breaks must never be coalesced: each has degree
    // multiplicity, so merging them produces multiplicity > degree+1.
    for (double offset : {1e-10, 4*std::numeric_limits<double>::epsilon()}) {
        const double a=0.5,b=a+offset;
        const std::vector<double> knots{0,0,0,0,a,a,a,b,b,b,1,1,1,1};
        std::vector<Vec3> poles;
        for (std::size_t i=0;i+4<knots.size();++i)
            poles.push_back({10*((knots[i+1]+knots[i+2]+knots[i+3])/3),0,0});
        auto curve=std::make_shared<BSplineCurve<3>>(3,knots,poles);
        LoftSection first,last; first.path={{curve,{0,1}}};
        last.path={{std::make_shared<Line<3>>(Vec3(0,0,10),Vec3(1,0,0)),{0,10}}};
        for (bool reverse : {false,true}) {
            const auto body=loftSheet(reverse?std::vector<LoftSection>{last,first}:std::vector<LoftSection>{first,last});
            FK_CHECK(checkBody(body).empty());
            for (double x : {0.0,2.5,5.0,7.5,10.0}) {
                double closest=1e300;
                for (EdgeId id : body.edges()) {
                    const auto &edge=body.edge(id);
                    closest=std::min(closest,projectPoint(*edge.curve,{x,0,0},edge.range).distance);
                }
                FK_CHECK(closest<1e-7);
            }
        }
    }
}

FK_TEST(LoftG1RoundoffTrimPiece) {
    const auto line=[](Vec3 a,Vec3 b)->PathSegment {
        return {std::make_shared<Line<3>>(a,normalized(b-a)),{0,distance(a,b)}};
    };
    auto source=std::make_shared<const Body>(ruledSurface({line({0,0,0},{10,0,0})},{line({0,-5,0},{10,-5,0})}));
    const double last=std::nextafter(1.0,0.0);
    auto curve=std::make_shared<BSplineCurve<3>>(3,std::vector<double>{0,0,0,0,last,last,last,1,1,1,1},
        std::vector<Vec3>{{0,0,0},{3,0,0},{7,0,0},{10,0,0},{10,0,0},{10,0,0},{10,0,0}});
    LoftSection first,second; first.path={{curve,{0,1}}}; second.path={line({0,10,5},{10,10,5})};
    for(bool reverse:{false,true}) {
        LoftOptions options;
        if(reverse) {options.endContinuity=1;options.endFaces={{source,source->faces().front()}};}
        else {options.startContinuity=1;options.startFaces={{source,source->faces().front()}};}
        const auto body=loftSheet(reverse?std::vector<LoftSection>{second,first}:std::vector<LoftSection>{first,second},options);
        FK_CHECK(checkBody(body).empty());
        for(FaceId face:body.faces()) for(double u:{0.1,0.5,0.9})
            FK_CHECK(std::fabs(normalAt(*body.face(face).surface,u,reverse?1:0).z())>1-1e-4);
    }
}

FK_TEST(LoftMultipleGuidesDoNotCreateRibbons) {
    const auto line=[](Vec3 a,Vec3 b)->PathSegment {return {std::make_shared<Line<3>>(a,normalized(b-a)),{0,distance(a,b)}};};
    // Dense exact section: modifying only the endpoint poles confines a
    // guide to a tiny strip and leaves a visible crease at the middle guide.
    std::vector<double> knots{0,0,0,0};
    for(int k=1;k<80;++k) knots.insert(knots.end(),3,k/80.0);
    knots.insert(knots.end(),4,1);
    std::vector<Vec3> poles;
    for(std::size_t j=0;j+4<knots.size();++j) poles.push_back({10*(knots[j+1]+knots[j+2]+knots[j+3])/3,0,0});
    auto profile=std::make_shared<BSplineCurve<3>>(3,knots,poles);
    LoftSection first,last; first.path={{profile,{0,1}}}; last.path={line({0,10,5},{10,10,5})};
    auto neighbor=std::make_shared<const Body>(ruledSurface({line({0,-5,0},{10,-5,0})},{line({0,0,0},{10,0,0})}));
    for(bool reverse:{false,true}) for(int continuity:{1,2}) for(bool contact:{false,true}) {
        LoftOptions options;options.guideContinuity=continuity;
        for(double x:{0.0,5.0,10.0}) {
            auto guide=std::make_shared<BSplineCurve<3>>(3,std::vector<double>{0,0,0,0,1,1,1,1},
                std::vector<Vec3>{{x,0,0},{x,4,0},{x,7,4},{x,10,5}});
            options.guides.push_back({{guide,{0,1}}});
        }
        if(contact) {
            if(reverse){options.endContinuity=1;options.endFaces={{neighbor,neighbor->faces().front()}};}
            else {options.startContinuity=1;options.startFaces={{neighbor,neighbor->faces().front()}};}
        }
        const auto body=loftSheet(reverse?std::vector<LoftSection>{last,first}:std::vector<LoftSection>{first,last},options);
        FK_CHECK(checkBody(body).empty());FK_CHECK(body.faces().size()==2);
        const auto &a=*body.face(body.faces()[0]).surface,&b=*body.face(body.faces()[1]).surface;
        for(int k=1;k<40;++k) {
            const double v=k/40.0;
            FK_CHECK(distance(a.point(1,v),b.point(0,v))<1e-8);
            FK_CHECK(norm(cross(normalAt(a,1,v),normalAt(b,0,v)))<1e-6);
            // No concentrated change between the guide and the interior.
            FK_CHECK(norm(cross(normalAt(a,0.95,v),normalAt(a,0.5,v)))<1e-5);
        }
        if(contact) for(FaceId f:body.faces()) for(double u:{0.1,0.5,0.9})
            FK_CHECK(std::fabs(normalAt(*body.face(f).surface,u,reverse?1:0).z())>1-1e-4);
    }
}


FK_TEST(LoftFollowsEntireCircularGuides) {
    const auto line=[](Vec3 a,Vec3 b)->PathSegment {return {std::make_shared<Line<3>>(a,normalized(b-a)),{0,distance(a,b)}};};
    LoftSection first,last;
    first.path={line({0,0,0},{10,0,0})};last.path={line({0,10,10},{10,10,10})};
    auto neighbor=std::make_shared<const Body>(ruledSurface({line({0,-5,0},{10,-5,0})},first.path));
    for(bool reverse:{false,true}) for(int continuity:{1,2}) for(bool rational:{false,true}) for(bool middle:{false,true}) {
        LoftOptions options;options.guideContinuity=continuity;
        for(double x:{0.0,5.0,10.0}) {
            auto curve=std::make_shared<Circle<3>>(Vec3(x,0,10),Vec3(0,1,0),Vec3(0,0,1),10);
            if(rational) {
                auto nurbs=std::make_shared<BSplineCurve<3>>(toBSpline(*curve,-kPi/2,0));
                options.guides.push_back({{nurbs,nurbs->domain()}});
            } else options.guides.push_back({{curve,{-kPi/2,0}}});
        }
        if(reverse) {options.endContinuity=1;options.endFaces={{neighbor,neighbor->faces().front()}};}
        else {options.startContinuity=1;options.startFaces={{neighbor,neighbor->faces().front()}};}
        std::vector<LoftSection> sections{first};
        if(middle) {
            const double y=10/std::sqrt(2.0),z=10-y;
            LoftSection mid;mid.path={line({0,y,z},{10,y,z})};sections.push_back(mid);
        }
        sections.push_back(last);if(reverse)std::reverse(sections.begin(),sections.end());
        const Body body=loftSheet(sections,options);
        FK_CHECK(checkBody(body).empty());
        FK_CHECK(body.faces().size()==2);
        for(const auto &guide:options.guides) {
            const auto &original=guide.front();
            EdgeId edge;
            for(EdgeId id:body.edges()) {
                const auto &e=body.edge(id);
                if(distance(e.curve->point(e.range.lo),original.curve->point(original.range.lo))<1e-7
                    && distance(e.curve->point(e.range.hi),original.curve->point(original.range.hi))<1e-7) edge=id;
                if(distance(e.curve->point(e.range.hi),original.curve->point(original.range.lo))<1e-7
                    && distance(e.curve->point(e.range.lo),original.curve->point(original.range.hi))<1e-7) edge=id;
            }
            FK_CHECK(edge.valid());if(!edge.valid())continue;
            const auto &e=body.edge(edge);
            for(int k=0;k<=128;++k) {
                const Vec3 p=e.curve->point(e.range.lo+e.range.length()*k/128.0);
                FK_CHECK(projectPoint(*original.curve,p,original.range).distance<1e-8);
                if(continuity==2 && (k==0 || k==128)) {
                    const double t=e.range.lo+e.range.length()*k/128.0;
                    const double u=projectPoint(*original.curve,p,original.range).parameter;
                    const auto curvature=[](const Curve<3> &c,double at) {
                        const Vec3 d1=c.derivative(at),d2=c.derivative(at,2),tangent=normalized(d1);
                        return (d2-dot(d2,tangent)*tangent)/dot(d1,d1);
                    };
                    FK_CHECK(norm(curvature(*e.curve,t)-curvature(*original.curve,u))<1e-7);
                }
                const Vec3 q=original.curve->point(original.range.lo+original.range.length()*k/128.0);
                FK_CHECK(projectPoint(*e.curve,q,e.range).distance<1e-8);
            }
        }
        const auto &a=*body.face(body.faces()[0]).surface,&b=*body.face(body.faces()[1]).surface;
        for(int k=1;k<40;++k) {
            const double v=k/40.0;
            FK_CHECK(distance(a.point(1,v),b.point(0,v))<1e-8);
            FK_CHECK(norm(cross(normalAt(a,1,v),normalAt(b,0,v)))<1e-6);
        }
        if(middle) for(FaceId f:body.faces()) for(double u:{0.1,0.5,0.9}) {
            const auto &surface=*body.face(f).surface;
            FK_CHECK_NEAR(surface.point(u,0.5).y(),10/std::sqrt(2.0),1e-8);
            FK_CHECK_NEAR(surface.point(u,0.5).z(),10-10/std::sqrt(2.0),1e-8);
        }
        for(FaceId f:body.faces()) for(double u:{0.1,0.5,0.9})
            FK_CHECK(std::fabs(normalAt(*body.face(f).surface,u,reverse?1:0).z())>1-1e-4);
    }
}

// Tre guide sulla stessa loft con condizioni diverse e influenza indipendente.
FK_TEST(LoftIndependentGuideConditions) {
    const auto line = [](Vec3 a, Vec3 b) -> PathSegment { return {std::make_shared<Line<3>>(a, normalized(b-a)), {0, distance(a,b)}}; };
    LoftSection first, last;
    first.path = {line({0,0,0},{10,0,0})}; last.path = {line({0,10,5},{10,10,5})};
    LoftOptions options;
    for (double x : {0.0, 5.0, 10.0}) {
        auto guide = std::make_shared<BSplineCurve<3>>(3, std::vector<double>{0,0,0,0,1,1,1,1},
            std::vector<Vec3>{{x,0,0},{x,4,0},{x,7,4},{x,10,5}});
        options.guides.push_back({{guide,{0,1}}});
    }
    options.guideConditions = {{0,1.0},{1,0.5},{2,1.0}};
    const Body body = loftSheet({first,last},options);
    FK_CHECK(checkBody(body).empty());
    const auto longitudinal = [&](const Body &shape, double x) -> const Edge * {
        for (auto id : shape.edges()) {
            const auto &edge = shape.edge(id);
            if (distance(edge.curve->point(edge.range.lo),{x,0,0}) < 1e-8 && distance(edge.curve->point(edge.range.hi),{x,10,5}) < 1e-8) return &edge;
        }
        return nullptr;
    };
    const Edge *free = longitudinal(body,0), *partial = longitudinal(body,5), *full = longitudinal(body,10);
    FK_CHECK(free && partial && full);
    if (!free || !partial || !full) return;
    const auto guide0 = options.guides[0][0].curve, guide1 = options.guides[1][0].curve, guide2 = options.guides[2][0].curve;
    const double gap0 = projectPoint(*free->curve,guide0->point(0.37),free->range).distance;
    const double gap1 = projectPoint(*partial->curve,guide1->point(0.37),partial->range).distance;
    FK_CHECK(gap0 > 0.1); FK_CHECK(gap1 > 1e-3 && gap1 < gap0);
    FK_CHECK(projectPoint(*full->curve,guide2->point(0.37),full->range).distance < 5e-9);
    FK_CHECK(norm(cross(normalized(full->curve->derivative(full->range.lo)), normalized(guide2->derivative(0)))) < 1e-8);
    // Riordino della lista: le condizioni seguono la propria curva.
    std::swap(options.guides[0],options.guides[2]); std::swap(options.guideConditions[0],options.guideConditions[2]);
    const Body reordered = loftSheet({first,last},options);
    FK_CHECK(checkBody(reordered).empty());
    for (double x : {0.0,5.0,10.0}) {
        const Edge *a = longitudinal(body,x), *b = longitudinal(reordered,x);
        FK_CHECK(a && b); if (!a || !b) continue;
        for (int i=0;i<=16;++i) FK_CHECK(projectPoint(*b->curve,a->curve->point(a->range.lo+i*a->range.length()/16.0),b->range).distance < 1e-8);
    }
    auto disabled = options;
    for (auto &condition : disabled.guideConditions) condition.influence = 0.0;
    const Body noInfluence=loftSheet({first,last},disabled);
    FK_CHECK(checkBody(noInfluence).empty());
    for (double x : {0.0,5.0,10.0}) {
        // La pezza piana puo' essere unificata: non pretendere un edge interno
        // a x=5. Verifica invece posizione e piano tangente del loft libero.
        const Vec3 point(x,3.7,1.85), expected=normalized(cross(Vec3(1,0,0),Vec3(0,10,5)));
        double gap=1e100; Vec3 normal;
        for (auto face : noInfluence.faces()) {
            const auto &surface=*noInfluence.face(face).surface;
            const auto near=projectPoint(surface,point);
            if(near.distance<gap) {gap=near.distance;normal=normalAt(surface,near.u,near.v);}
        }
        FK_CHECK(gap < 1e-8);FK_CHECK(std::fabs(dot(normal,expected)) > 1-1e-10);
    }
    auto invalid = options; invalid.guideConditions.pop_back();
    bool rejected = false; try { (void)loftSheet({first,last},invalid); } catch (const std::domain_error &) { rejected=true; }
    FK_CHECK(rejected);
}

// Un nodo guida vicino alla sezione entro l'arrotondamento non deve alterare
// la normale della superficie, anche se posizione e tangente guida sono esatte.
FK_TEST(LoftGuideNearSectionPreservesContactPlane) {
    const auto profile=[](double x) -> PathSegment {
        auto c=std::make_shared<BSplineCurve<3>>(3,std::vector<double>{0,0,0,0,1,1,1,1},
            std::vector<Vec3>{{x,0,0},{x,3,0},{x,7,2},{x,10,2}});
        return {c,{0,1}};
    };
    LoftSection first,last;
    for(auto *section : {&first,&last}) {
        const double x=section==&first?0:20;
        section->frame=Frame3(Vec3(x,0,0),Vec3(1,0,0),Vec3(0,1,0));
        auto c=std::make_shared<BSplineCurve<2>>(3,std::vector<double>{0,0,0,0,1,1,1,1},
            std::vector<Vec2>{{0,0},{3,0},{7,2},{10,2}});
        section->loop.segments={{c,{0,1}}};
    }
    auto support=std::make_shared<const Body>(ruledSurface({profile(20)},{profile(25)}));
    for(int continuity:{1,2}) for(bool reverse:{false,true}) {
        LoftOptions options;options.guideContinuity=continuity;
        const double split=std::nextafter(std::nextafter(20.0,0.0),0.0);
        for(Vec3 origin:{Vec3(0,0,0),Vec3(0,10,2)}) {
            auto c=std::make_shared<BSplineCurve<3>>(1,std::vector<double>{-5,-5,split,25,25},
                std::vector<Vec3>{origin+Vec3(-5,0,0),origin+Vec3(split,0,0),origin+Vec3(25,0,0)});
            options.guides.push_back({{c,{-5,25}}});
        }
        if(reverse) {options.startContinuity=1;options.startFaces={{support,support->faces().front()}};}
        else {options.endContinuity=1;options.endFaces={{support,support->faces().front()}};}
        const auto body=loftSheet(reverse?std::vector<LoftSection>{last,first}:std::vector<LoftSection>{first,last},options);
        FK_CHECK(checkBody(body).empty());
        for(FaceId f:body.faces()) {
            const auto &surface=*body.face(f).surface;
            const auto breaks=surface.vBreakpoints(surface.vDomain());
            for(std::size_t k=1;k<breaks.size();++k) FK_CHECK(breaks[k]-breaks[k-1]>1e-12);
            for(double u:{0.0,0.1,0.5,0.9,1.0}) {
                const Vec3 point=surface.point(u,reverse?0:1);
                const auto projected=projectPoint(*support->face(support->faces().front()).surface,point);
                FK_CHECK(projected.distance<1e-8);
                const Vec3 expected=normalAt(*support->face(support->faces().front()).surface,projected.u,projected.v);
                FK_CHECK(norm(cross(normalAt(surface,u,reverse?0:1),expected))<1e-7);
            }
            for(double v:{0.0,0.17,0.51,0.83,1.0}) for(double u:{0.0,1.0}) {
                const auto point=surface.point(u,v);
                FK_CHECK_NEAR(point.y(),u==0?0.0:10.0,1e-8);
                FK_CHECK_NEAR(point.z(),u==0?0.0:2.0,1e-8);
            }
        }
    }
}
