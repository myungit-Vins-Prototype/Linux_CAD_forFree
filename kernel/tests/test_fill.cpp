#include <cmath>
#include <algorithm>
#include <stdexcept>
#include <limits>

#include "fk_bspline_surface.h"

#include "fk_body_check.h"
#include "fk_fill.h"
#include "fk_mass.h"
#include "fk_primitives.h"
#include "fk_revolve.h"
#include "fk_sew.h"
#include "fk_surface_algo.h"
#include "fk_tessellate.h"
#include "fk_test.h"

using namespace ForgeCad::Kernel;

namespace {


PathSegment line(const Vec3 &a, const Vec3 &b) { return {std::make_shared<Line<3>>(a, b - a), {0.0, distance(a, b)}}; }

PathSegment circle(const Vec3 &center, const Vec3 &x, const Vec3 &y, double radius, double from, double to) {
    return {std::make_shared<Circle<3>>(center, x, y, radius), {from, to}};
}

void checkValid(const Body &body) {
    FK_CHECK(body.isSheet());
    FK_CHECK(body.faces().size() == 1);
    for (const CheckIssue &issue : checkBody(body)) fktest::reportFailure(__FILE__, __LINE__, describe(issue.code) + ": " + issue.message);
    TessellationOptions options;
    options.deflection = 1e-3;
    FK_CHECK(tessellate(body, options).failedFaces == 0);
}

// Lamina con la sola faccia laterale di un solido di rivoluzione.
std::shared_ptr<const Body> sideSheet(const Body &solid, SurfaceType type, FaceId &face) {
    std::vector<FaceId> sides;
    for (FaceId f : solid.faces())
        if (solid.face(f).surface->type() == type) sides.push_back(f);
    auto sheet = std::make_shared<const Body>(facesAsSheet(solid, sides));
    for (FaceId f : sheet->faces()) face = f;
    return sheet;
}

double surfaceDistance(const Body &body, const Vec3 &p) {
    double best = 1e300;
    for (FaceId f : body.faces()) best = std::min(best, projectPoint(*body.face(f).surface, p).distance);
    return best;
}

}

// Contorno piano senza guide: il piano (la lastra di energia minima).
FK_TEST(FillPlanarSquare) {
    const Frame3 frame(Vec3(1, -2, 0.5), Vec3(0.2, 0.3, 1.0), Vec3(1, 0, 0));
    const auto at = [&](double x, double y) { return frame.toGlobal(Vec3(x, y, 0)); };
    const std::vector<PathSegment> pieces{line(at(0, 0), at(2, 0)), line(at(2, 0), at(2, 1.5)), line(at(0, 1.5), at(2, 1.5)), line(at(0, 0), at(0, 1.5))};
    FillReport report;
    const Body body = fillSurface(pieces, {}, {}, {}, &report);
    checkValid(body);
    FK_CHECK(report.boundaryDeviation < 1e-6);
    FK_CHECK_NEAR(faceArea(body, body.faces().front()), 3.0, 1e-6);
    for (double x : {0.3, 1.0, 1.7})
        for (double y : {0.2, 0.75, 1.3}) {
            const SurfaceProjection p = projectPoint(*body.face(body.faces().front()).surface, at(x, y));
            FK_CHECK(p.distance < 1e-6);
        }
}

// Calotta sferica: cerchio di bordo e due archi di meridiano che si
// incrociano nel polo. La superficie passa per tutte e tre le curve.
FK_TEST(FillSphericalCapThroughGuides) {
    const Vec3 c(-1, 2, 0);
    const double r = 1.5, height = 0.925, rim = std::sqrt(r * r - height * height);
    const Vec3 x(1, 0, 0), y(0, 1, 0), z(0, 0, 1);
    const Vec3 rimCenter = c + height * y;
    const std::vector<PathSegment> boundary{circle(rimCenter, x, z, rim, 0.0, 2.0 * kPi)};
    const double a = std::atan2(height, rim);
    // Archi nei piani xy e zy per il centro, dal bordo al bordo passando per il polo.
    const std::vector<PathSegment> guides{circle(c, x, y, r, a, kPi - a), circle(c, z, y, r, a, kPi - a)};
    FillReport report;
    const Body body = fillSurface(boundary, guides, {}, {}, &report);
    checkValid(body);
    FK_CHECK(report.boundaryDeviation < 1e-6);
    FK_CHECK(report.guideDeviation < 1e-6);
    // Fuori dalle guide la lastra non e' la sfera, ma le e' vicina.
    const double theta = 0.5 * std::atan2(rim, height);  // a meta' tra polo e bordo
    for (double phi : {0.4, 1.2, 2.0, 3.6, 5.5}) {
        const Vec3 p = c + r * (std::sin(theta) * (std::cos(phi) * x + std::sin(phi) * z) + std::cos(theta) * y);
        FK_CHECK(surfaceDistance(body, p) < 0.05);
    }
}

// Tubo cilindrico chiuso in tangenza: sul bordo la normale e' quella del cilindro.
FK_TEST(FillTangentToCylinder) {
    const Frame3 frame(Vec3(0, 0, 0), Vec3(0, 0, 1), Vec3(1, 0, 0));
    FaceId side;
    const auto tube = sideSheet(makeCylinder(frame, 1.0, 2.0), SurfaceType::Cylinder, side);
    const std::vector<PathSegment> boundary{circle(Vec3(0, 0, 2), Vec3(1, 0, 0), Vec3(0, 1, 0), 1.0, 0.0, 2.0 * kPi)};
    for (int continuity : {1, 2}) {
        FillOptions options;
        options.continuity = continuity;
        FillReport report;
        const Body body = fillSurface(boundary, {}, {{tube, side}}, options, &report);
        checkValid(body);
        FK_CHECK(report.contactPieces == 1);
        FK_CHECK(report.boundaryDeviation < 1e-6);
        FK_CHECK(report.tangentAngle < 0.5 * kPi / 180.0);
        // Il tappo sale sopra il bordo (continua le pareti verticali).
        const Surface &s = *body.face(body.faces().front()).surface;
        FK_CHECK(s.point(0.0, 0.0).z() > 2.1);
        // Cucito al tubo: un solo bordo libero (quello in basso).
        const SewResult sewn = sewSheets({tube.get(), &body}, 1e-6, false);
        FK_CHECK(sewn.freeEdges == 1);
    }
}

// Il caso di "superfice piana influenzata.prt": tronco di cono aperto in
// alto, tappo tangente al cono con due archi guida su una sfera. Le guide
// arrivano sul bordo con un angolo diverso dalla tangenza: il rapporto lo dice.
FK_TEST(FillConeWithConflictingGuides) {
    const Frame3 frame(Vec3(-0.9584606322, 1.000132852, 0), Vec3(0, 1, 0), Vec3(1, 0, 0));
    FaceId side;
    const double top = 1.180712488, bottom = 1.9724843912;
    const auto cone = sideSheet(makeCone(frame, bottom, top, 1.937757553), SurfaceType::Cone, side);
    const Vec3 c(-0.9584606322, 2.012732028, 0), x(1, 0, 0), y(0, 1, 0), z(0, 0, 1);
    const double r = 1.5, height = 2.937890405 - 2.012732028;
    const std::vector<PathSegment> boundary{circle(c + height * y, x, z, top, 0.0, 2.0 * kPi)};
    const double a = std::atan2(height, top);
    const std::vector<PathSegment> guides{circle(c, x, y, r, a, kPi - a), circle(c, z, y, r, a, kPi - a)};
    FillOptions options;
    FillReport report;
    const Body body = fillSurface(boundary, guides, {{cone, side}}, options, &report);
    checkValid(body);
    FK_CHECK(report.contactPieces == 1);
    FK_CHECK(report.boundaryDeviation < 1e-5);
    bool conflict = false;
    for (const std::string &note : report.notes) conflict = conflict || note.find("non sono compatibili") != std::string::npos;
    FK_CHECK(conflict);
    std::printf("cono: celle %d, contorno %.3g, guide %.3g, tangenza %.3g gradi\n", report.spans, report.boundaryDeviation,
                report.guideDeviation, report.tangentAngle * 180.0 / kPi);
    // Senza guide la tangenza e' rispettata.
    FillReport free;
    const Body cap = fillSurface(boundary, {}, {{cone, side}}, options, &free);
    checkValid(cap);
    FK_CHECK(free.tangentAngle < 0.5 * kPi / 180.0);
    std::printf("cono senza guide: celle %d, contorno %.3g, tangenza %.3g gradi\n", free.spans, free.boundaryDeviation, free.tangentAngle * 180.0 / kPi);
}

// Calotta che continua una zona sferica in curvatura con le guide sulla
// stessa sfera: condizioni compatibili, la superficie resta vicina alla sfera.
FK_TEST(FillCapContinuesSphereInCurvature) {
    const Vec3 c(-1, 2, 0), x(1, 0, 0), y(0, 1, 0), z(0, 0, 1);
    const double r = 1.5, height = 0.925, rim = std::sqrt(r * r - height * height);
    const double top = std::asin(height / r);
    ProfileLoop chain;
    chain.segments.push_back({std::make_shared<Circle<2>>(makeCircle(Vec2(0, 0), r)), {-kPi / 6.0, top}});
    const auto zone = std::make_shared<const Body>(makeSheetRevolution(Frame3(c, y, x), {chain}, 2.0 * kPi));
    FaceId face;
    for (FaceId f : zone->faces()) face = f;
    const std::vector<PathSegment> boundary{circle(c + height * y, x, z, rim, 0.0, 2.0 * kPi)};
    const double a = std::atan2(height, rim);
    const std::vector<PathSegment> guides{circle(c, x, y, r, a, kPi - a), circle(c, z, y, r, a, kPi - a)};
    FillOptions options;
    options.continuity = 2;
    FillReport report;
    const Body body = fillSurface(boundary, guides, {{zone, face}}, options, &report);
    checkValid(body);
    FK_CHECK(report.contactPieces == 1);
    for (const std::string &note : report.notes) FK_CHECK(note.find("non sono compatibili") == std::string::npos);
    FK_CHECK(report.boundaryDeviation < 1e-6);
    FK_CHECK(report.tangentAngle < 0.05 * kPi / 180.0);
    FK_CHECK(report.curvatureDeviation < 0.01);
    double worst = 0.0;
    const double theta = 0.5 * std::atan2(rim, height);
    for (double phi : {0.4, 1.2, 2.0, 3.6, 5.5}) {
        const Vec3 p = c + r * (std::sin(theta) * (std::cos(phi) * x + std::sin(phi) * z) + std::cos(theta) * y);
        worst = std::max(worst, surfaceDistance(body, p));
    }
    std::printf("sfera G2: celle %d, contorno %.3g, guide %.3g, tangenza %.3g gradi, curvatura %.3g, dalla sfera %.3g\n", report.spans,
                report.boundaryDeviation, report.guideDeviation, report.tangentAngle * 180.0 / kPi, report.curvatureDeviation, worst);
    FK_CHECK(worst < 0.01);
}

namespace {

// Tre facce adiacenti a una patch bilineare, con parametrizzazione obliqua
// rispetto al piano medio e versi delle normali indipendenti.
struct FillSaddleFixture {
    Frame3 frame{Vec3(3, -6, 8), Vec3(0.2, -0.3, 1), Vec3(1, 0.1, 0)};
    std::vector<PathSegment> boundary;
    std::vector<FillContact> contacts;

    Vec3 point(double x, double y) const { return frame.toGlobal(Vec3(x, y, 0.2 * x + 0.3 * y + 0.02 * x * y)); }

    std::shared_ptr<const Body> sheet(double x0, double x1, double y0, double y1, bool reversed) const {
        auto surface = std::make_shared<BSplineSurface>(1, 1, std::vector<double>{x0, x0, x1, x1},
            std::vector<double>{y0, y0, y1, y1}, 2, 2,
            std::vector<Vec3>{point(x0, y0), point(x0, y1), point(x1, y0), point(x1, y1)});
        const std::vector<Vec2> uv{{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
        std::vector<Vec3> vertices;
        for (const Vec2 &p : uv) vertices.push_back(point(p.x(), p.y()));
        std::vector<Body::BuildEdge> edges;
        std::vector<Body::BuildFin> fins;
        for (int i = 0; i < 4; ++i) {
            const int j = (i + 1) % 4;
            const PathSegment segment = line(vertices[i], vertices[j]);
            edges.push_back({i, j, segment.curve, segment.range});
            auto pcurve = std::make_shared<BSplineCurve<2>>(1,
                std::vector<double>{0, 0, segment.range.hi, segment.range.hi}, std::vector<Vec2>{uv[i], uv[j]});
            fins.push_back({i, !reversed, pcurve});
        }
        if (reversed) std::reverse(fins.begin(), fins.end());
        return std::make_shared<const Body>(Body::buildSheet(vertices, edges, {{surface, !reversed, {fins}}}));
    }

    explicit FillSaddleFixture(bool reversed = false) {
        const Vec3 a = point(0, 0), b = point(36, 0), c = point(36, 13), d = point(0, 13);
        boundary = {line(a, b), line(b, c), line(c, d), line(d, a)};
        for (const auto &body : {sheet(0, 36, -2, 0, reversed), sheet(36, 38, 0, 13, !reversed), sheet(0, 36, 13, 15, reversed)})
            contacts.push_back({body, body->faces().front()});
        if (reversed) {
            // Cambia anche il verso del contorno, senza cambiare la geometria.
            boundary[0] = line(b, a);
            std::reverse(boundary.begin(), boundary.end());
        }
    }
};

}

FK_TEST(FillObliqueContactsPreserveInterior) {
    for (bool reversed : {false, true})
        for (int continuity : {1, 2}) {
            const FillSaddleFixture fixture(reversed);
            FillOptions options;
            options.continuity = continuity;
            options.maxSpans = 32;
            FillReport report;
            const Body body = fillSurface(fixture.boundary, {}, fixture.contacts, options, &report);
            checkValid(body);
            FK_CHECK(report.contactPieces == 3);
            FK_CHECK(report.boundaryDeviation < 1e-4);
            FK_CHECK(report.tangentAngle < 0.1 * kPi / 180.0);
            if (continuity == 2) FK_CHECK(report.curvatureDeviation < 1e-3);
            // I punti interni della mesh restano nella patch; nessuna punta
            // oltre il contorno. Controllo geometrico indipendente dal rapporto.
            TessellationOptions meshOptions;
            meshOptions.deflection = 0.02;
            const Tessellation mesh = tessellate(body, meshOptions);
            for (const FaceMesh &face : mesh.faces)
                for (const Vec3 &p : face.points) {
                    const Vec3 local = fixture.frame.toLocal(p);
                    FK_CHECK(local.x() >= -1e-3 && local.x() <= 36 + 1e-3);
                    FK_CHECK(local.y() >= -1e-3 && local.y() <= 13 + 1e-3);
                    FK_CHECK(local.z() >= -1e-3 && local.z() <= 21 + 1e-3);
                }
        }
}

// L'influenza controlla la forma, senza scalare la proiezione della G1.
// La vecchia implementazione ripiegava questa patch gia' con influenza 2.
FK_TEST(FillInfluencePreservesProjectionAndTangency) {
    const FillSaddleFixture fixture;
    Vec3 n;
    for (std::size_t i = 0; i < fixture.boundary.size(); ++i)
        n = n + cross(fixture.boundary[i].curve->point(fixture.boundary[i].range.lo),
                      fixture.boundary[(i + 1) % fixture.boundary.size()].curve->point(fixture.boundary[(i + 1) % fixture.boundary.size()].range.lo));
    n = normalized(n);
    for (double influence : {0.05, 0.5, 2.0, 10.0, 20.0}) {
        FillOptions options;
        options.influence = influence;
        options.maxSpans = 32;
        FillReport report;
        const Body body = fillSurface(fixture.boundary, {}, fixture.contacts, options, &report);
        checkValid(body);
        FK_CHECK(report.boundaryDeviation < 1e-4);
        FK_CHECK(report.tangentAngle < 0.1 * kPi / 180.0);
        const Surface &surface = *body.face(body.faces().front()).surface;
        for (int i = 1; i < 8; ++i)
            for (int j = 1; j < 8; ++j) {
                Vec3 d[4];
                surface.evaluate(surface.uDomain().lo + surface.uDomain().length() * i / 8.0,
                                 surface.vDomain().lo + surface.vDomain().length() * j / 8.0, 1, d);
                const Vec3 su = d[Surface::derivativeIndex(1, 0, 1)], sv = d[Surface::derivativeIndex(0, 1, 1)];
                // Jacobiano identicamente 1 anche tra i dati e fuori dal trim.
                FK_CHECK_NEAR(dot(cross(su, sv), n), 1.0, 1e-10);
            }
    }
}

// G1 lascia libera la curvatura trasversale. Il raccordo naturale deve pero'
// seguire il supporto: una semplice lastra minima produceva sul bordo una
// curvatura diversa di oltre 0.4 1/mm, pur avendo normali tangenti corrette.
FK_TEST(FillNaturalG1FollowsSupportCurvature) {
    const Vec3 c(-1, 2, 0), x(1, 0, 0), y(0, 1, 0), z(0, 0, 1);
    const double r = 1.5, height = 0.925, rim = std::sqrt(r * r - height * height);
    ProfileLoop chain;
    chain.segments.push_back({std::make_shared<Circle<2>>(makeCircle(Vec2(0, 0), r)), {-kPi / 6.0, std::asin(height / r)}});
    const auto zone = std::make_shared<const Body>(makeSheetRevolution(Frame3(c, y, x), {chain}, 2.0 * kPi));
    const std::vector<PathSegment> boundary{circle(c + height * y, x, z, rim, 0.0, 2.0 * kPi)};
    FillOptions options;
    options.continuity = 1;
    FillReport report;
    const Body body = fillSurface(boundary, {}, {{zone, zone->faces().front()}}, options, &report);
    checkValid(body);
    FK_CHECK(report.boundaryDeviation < 1e-6);
    FK_CHECK(report.tangentAngle < 0.05 * kPi / 180.0);
    const Surface &surface = *body.face(body.faces().front()).surface;
    double worst = 0.0;
    for (int i = 0; i < 64; ++i) {
        Vec3 curve[2];
        boundary.front().curve->evaluate(2 * kPi * (i + 0.37) / 64, 1, curve);
        const SurfaceProjection near = projectPoint(surface, curve[0]);
        Vec3 d[9];
        surface.evaluate(near.u, near.v, 2, d);
        const Vec3 su = d[Surface::derivativeIndex(1, 0, 2)], sv = d[Surface::derivativeIndex(0, 1, 2)];
        const Vec3 normal = normalized(cross(su, sv));
        const Vec3 direction = normalized(cross(normal, curve[1]));
        const double a = dot(su, su), b = dot(su, sv), e = dot(sv, sv), determinant = a * e - b * b;
        const double u = (e * dot(su, direction) - b * dot(sv, direction)) / determinant;
        const double v = (a * dot(sv, direction) - b * dot(su, direction)) / determinant;
        const Vec3 second = u * u * d[Surface::derivativeIndex(2, 0, 2)]
                          + 2 * u * v * d[Surface::derivativeIndex(1, 1, 2)] + v * v * d[Surface::derivativeIndex(0, 2, 2)];
        // Sfera: II(D,D) = -N.(P-C)/r^2, con D unitario nel piano tangente.
        const double expected = -dot(normal, curve[0] - c) / (r * r);
        worst = std::max(worst, std::fabs(dot(normal, second) - expected));
    }
    std::printf("sfera G1 naturale: scarto curvatura %.3g 1/mm\n", worst);
    FK_CHECK(worst < 0.01);
}

FK_TEST(FillRejectsInvalidShapeParameters) {
    const FillSaddleFixture fixture;
    for (double influence : {0.0, -1.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        FillOptions options;
        options.influence = influence;
        bool rejected = false;
        try { (void)fillSurface(fixture.boundary, {}, fixture.contacts, options); }
        catch (const std::domain_error &) { rejected = true; }
        FK_CHECK(rejected);
    }
}

FK_TEST(FillIndependentContactInfluences) {
    const FillSaddleFixture fixture;
    auto contacts = fixture.contacts;
    contacts[0].influence=0.2; contacts[1].influence=1.7; contacts[2].influence=3.0;
    FillOptions options; options.maxSpans=32; options.influence=0.5;
    FillReport a, b;
    const Body first = fillSurface(fixture.boundary,{},contacts,options,&a);
    options.influence=5.0;
    const Body second = fillSurface(fixture.boundary,{},contacts,options,&b);
    checkValid(first); checkValid(second);
    FK_CHECK(a.tangentAngle < 0.1*kPi/180); FK_CHECK(a.boundaryDeviation < 1e-4);
    const auto &sa=*first.face(first.faces().front()).surface, &sb=*second.face(second.faces().front()).surface;
    // Impostazioni esplicite per faccia: il default globale non le sovrascrive.
    for (int i=1;i<8;++i) for (int j=1;j<8;++j) {
        const double u=sa.uDomain().lo+sa.uDomain().length()*i/8.0, v=sa.vDomain().lo+sa.vDomain().length()*j/8.0;
        FK_CHECK(distance(sa.point(u,v),sb.point(u,v)) < 1e-10);
    }
    contacts[1].influence=std::numeric_limits<double>::quiet_NaN();
    bool rejected=false;try{(void)fillSurface(fixture.boundary,{},contacts,options);}catch(const std::domain_error &){rejected=true;}
    FK_CHECK(rejected);
}

FK_TEST(FillIgnoredGuidesPreserveLocalInfluence) {
    const FillSaddleFixture fixture;
    FillOptions options;options.guideWeight=0;options.maxSpans=32;
    const std::vector<PathSegment> guides{line(fixture.point(1,6),fixture.point(35,6))};
    const Body free=fillSurface(fixture.boundary,{},fixture.contacts,options);
    const Body ignored=fillSurface(fixture.boundary,guides,fixture.contacts,options);
    const auto &a=*free.face(free.faces().front()).surface,&b=*ignored.face(ignored.faces().front()).surface;
    for(int i=1;i<8;++i)for(int j=1;j<8;++j) {
        const double u=a.uDomain().lo+i*a.uDomain().length()/8.,v=a.vDomain().lo+j*a.vDomain().length()/8.;
        FK_CHECK(distance(a.point(u,v),b.point(u,v))<1e-10);
    }
}

FK_TEST(FillDirectionalProfilePreservesContacts) {
    for(bool reversed:{false,true}){
        const FillSaddleFixture fixture(reversed);FillOptions options;options.maxSpans=32;options.shapeContact=0;
        FillReport report;const Body body=fillSurface(fixture.boundary,{},fixture.contacts,options,&report);
        checkValid(body);FK_CHECK(report.contactPieces==3);FK_CHECK(report.boundaryDeviation<1e-4);FK_CHECK(report.tangentAngle<0.1*kPi/180);
        for(int i=1;i<8;++i)for(int j=1;j<8;++j)FK_CHECK(surfaceDistance(body,fixture.point(36*i/8.,13*j/8.))<0.03);
        options.shapeWeight=0;const Body ignored=fillSurface(fixture.boundary,{},fixture.contacts,options);
        options.shapeContact=-1;const Body natural=fillSurface(fixture.boundary,{},fixture.contacts,options);
        const auto &a=*ignored.face(ignored.faces().front()).surface;const auto &b=*natural.face(natural.faces().front()).surface;
        for(double u:{0.2,0.5,0.8})for(double v:{0.2,0.5,0.8})FK_CHECK(distance(a.point(a.uDomain().lo+u*a.uDomain().length(),a.vDomain().lo+v*a.vDomain().length()),b.point(b.uDomain().lo+u*b.uDomain().length(),b.vDomain().lo+v*b.vDomain().length()))<1e-10);
    }
}
FK_TEST(FillDirectionalProfileRejectsInvalidSelection) {
    const FillSaddleFixture fixture;
    for(int selected:{-2,3}){FillOptions options;options.shapeContact=selected;bool caught=false;try{fillSurface(fixture.boundary,{},fixture.contacts,options);}catch(const std::domain_error&){caught=true;}FK_CHECK(caught);}
    FillOptions options;options.shapeContact=0;options.continuity=0;bool caught=false;try{fillSurface(fixture.boundary,{},fixture.contacts,options);}catch(const std::domain_error&){caught=true;}FK_CHECK(caught);
}
