#include <cmath>

#include "fk_body_check.h"
#include "fk_boundary.h"
#include "fk_loft.h"
#include "fk_mass.h"
#include "fk_tessellate.h"
#include "fk_test.h"

using namespace ForgeCad::Kernel;

namespace {

PathSegment line(const Vec3 &a, const Vec3 &b) { return {std::make_shared<Line<3>>(a, b - a), {0.0, distance(a, b)}}; }

PathSegment arc(const Frame3 &frame, const Vec2 &center, double radius, double from, double to) {
    const Vec3 c = frame.origin() + center.x() * frame.xDir() + center.y() * frame.yDir();
    return {std::make_shared<Circle<3>>(c, frame.xDir(), frame.yDir(), radius), {from, to}};
}

Vec3 at(const Frame3 &frame, double x, double y) { return frame.origin() + x * frame.xDir() + y * frame.yDir(); }

double area(const Body &body) {
    double total = 0.0;
    for (FaceId f : body.faces()) total += faceArea(body, f);
    return total;
}

// Lamina valida di una faccia, area attesa, tassellazione.
void checkSheet(const std::vector<PathSegment> &pieces, double expected, double relative) {
    Body body;
    try {
        body = boundarySheet(pieces);
    } catch (const std::exception &error) {
        fktest::reportFailure(__FILE__, __LINE__, error.what());
        return;
    }
    FK_CHECK(body.isSheet());
    FK_CHECK(body.faces().size() == 1);
    for (const CheckIssue &issue : checkBody(body)) fktest::reportFailure(__FILE__, __LINE__, describe(issue.code) + ": " + issue.message);
    FK_CHECK_NEAR(area(body), expected, relative * expected);
    TessellationOptions options;
    options.deflection = 1e-3;
    FK_CHECK(tessellate(body, options).failedFaces == 0);
}

const Frame3 kTilted(Vec3(3, -1, 2), Vec3(0.3, -0.4, 1.0), Vec3(1, 0.2, 0));

}

// Quattro rette sghembe: la patch di Coons e' il paraboloide iperbolico,
// la stessa superficie della rigata tra due lati opposti.
FK_TEST(BoundarySheetSkewQuad) {
    const Vec3 a(0, 0, 0), b(1, 0, 0), c(1, 1, 1), d(0, 1, 0);
    // Tratti in ordine e verso qualsiasi.
    const std::vector<PathSegment> pieces{line(c, b), line(a, b), line(d, a), line(c, d)};
    const Body ruled = ruledSurface({line(a, b)}, {line(d, c)});
    checkSheet(pieces, area(ruled), 1e-8);
}

// Contorni piani: la patch sta nel piano e copre la regione (area esatta).
FK_TEST(BoundarySheetPlanarRegions) {
    // Quadrato 2 x 2 con il lato superiore sostituito da un arco di raggio
    // sqrt(2) attorno al centro: quattro angoli vivi, area 3 + pi/2.
    const double r = std::sqrt(2.0);
    checkSheet({line(at(kTilted, 0, 0), at(kTilted, 2, 0)), line(at(kTilted, 2, 0), at(kTilted, 2, 2)),
                arc(kTilted, Vec2(1, 1), r, 0.25 * kPi, 0.75 * kPi), line(at(kTilted, 0, 2), at(kTilted, 0, 0))},
               3.0 + 0.5 * kPi, 1e-8);
    // Triangolo: tre lati, il quarto degenere in un angolo.
    checkSheet({line(at(kTilted, 0, 0), at(kTilted, 2, 0)), line(at(kTilted, 2, 0), at(kTilted, 0, 2)), line(at(kTilted, 0, 2), at(kTilted, 0, 0))},
               2.0, 1e-8);
    // Cerchio intero (nessun angolo): quattro quarti, il disco. Sui bordi curvi
    // l'area passa dalle SP-curve approssimate (kPCurveTolerance 1e-7).
    checkSheet({arc(kTilted, Vec2(0, 0), 1.5, 0.0, kTwoPi)}, kPi * 2.25, 1e-7);
    // Due archi (due angoli): la lente di due cerchi di raggio 1 a distanza 1.
    const double h = std::sqrt(3.0) / 2.0;
    const double lens = 2.0 * (kPi / 3.0 - 0.5 * h);
    checkSheet({arc(kTilted, Vec2(0, 0), 1.0, -kPi / 3.0, kPi / 3.0), arc(kTilted, Vec2(1, 0), 1.0, 2.0 * kPi / 3.0, 4.0 * kPi / 3.0)}, lens, 1e-7);
}

// Due semicerchi in piani paralleli e due rette: la patch e' il mezzo cilindro.
FK_TEST(BoundarySheetHalfCylinder) {
    const Frame3 front(Vec3(1, 0, 0), Vec3(0, -1, 0), Vec3(1, 0, 0)), back(Vec3(1, 2, 0), Vec3(0, -1, 0), Vec3(1, 0, 0));
    // Nel piano XZ (normale -Y): x = asse X, y = asse Z.
    checkSheet({arc(front, Vec2(0, 0), 1.0, 0.0, kPi), line(Vec3(0, 0, 0), Vec3(0, 2, 0)), arc(back, Vec2(0, 0), 1.0, 0.0, kPi),
                line(Vec3(2, 2, 0), Vec3(2, 0, 0))},
               2.0 * kPi, 1e-7);
}

FK_TEST(BoundarySheetRejectsBadContours) {
    // Aperto.
    FK_CHECK_THROWS(boundarySheet({line(Vec3(0, 0, 0), Vec3(1, 0, 0)), line(Vec3(1, 0, 0), Vec3(1, 1, 0))}));
    // Pentagono: cinque angoli vivi.
    std::vector<PathSegment> pentagon;
    for (int k = 0; k < 5; ++k) {
        const double a0 = kTwoPi * k / 5.0, a1 = kTwoPi * (k + 1) / 5.0;
        pentagon.push_back(line(Vec3(std::cos(a0), std::sin(a0), 0), Vec3(std::cos(a1), std::sin(a1), 0)));
    }
    FK_CHECK_THROWS(boundarySheet(pentagon));
}
