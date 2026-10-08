#include <cmath>

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
