#include <cmath>

#include "fk_body_check.h"
#include "fk_bspline_surface.h"
#include "fk_extrude.h"
#include "fk_offset.h"
#include "fk_sheet.h"
#include "fk_mass.h"
#include "fk_primitives.h"
#include "fk_profile.h"
#include "fk_sew.h"
#include "fk_test.h"
#include "fk_thicken.h"

using namespace ForgeCad::Kernel;

namespace {

void checkSolid(const Body &body) {
    FK_CHECK(!body.isSheet());
    for (const CheckIssue &issue : checkBody(body)) fktest::reportFailure(__FILE__, __LINE__, describe(issue.code) + ": " + issue.message);
}

// Faccia della scatola [0, 10] x [0, 6] x [0, 4] con la normale data.
FaceId boxFace(const Body &box, const Vec3 &normal) {
    for (FaceId f : box.faces()) {
        const Frame3 &frame = static_cast<const Plane &>(*box.face(f).surface).frame();
        const Vec3 n = box.face(f).sense ? frame.zDir() : -1.0 * frame.zDir();
        if (dot(n, normal) > 0.999) return f;
    }
    return FaceId();
}

// Lamina cilindrica: arco di raggio R (angoli a..b attorno all'asse X, 0 lungo
// +Y e pi/2 lungo +Z) estruso per L lungo X.
Body arcSheet(double R, double a, double b, double L) {
    const Frame3 frame(Vec3(), Vec3(1.0, 0.0, 0.0), Vec3(0.0, 1.0, 0.0));
    ProfileLoop chain;
    chain.segments.push_back({std::make_shared<Circle<2>>(makeCircle(Vec2(0.0, 0.0), R)), {a, b}});
    return makeSheetExtrusion(frame, {chain}, L);
}

double zRange(const Body &body, bool upper) {
    double value = upper ? -1e300 : 1e300;
    for (VertexId v : body.vertices()) value = upper ? std::max(value, body.vertex(v).point.z()) : std::min(value, body.vertex(v).point.z());
    return value;
}

}

// Faccia piana: volume esatto e lato dello spessore (normale, opposto, meta' per parte).
FK_TEST(ThickenPlanarFaceSides) {
    const Body box = makeBox(Frame3(), 10.0, 6.0, 4.0);
    const FaceId top = boxFace(box, Vec3(0.0, 0.0, 1.0));
    FK_CHECK(top.valid());
    ThickenOptions options;
    options.thickness = 2.0;
    const Body up = thickenSheet(box, {top}, options);
    checkSolid(up);
    FK_CHECK_NEAR(massProperties(up).volume, 120.0, 1e-9);
    FK_CHECK_NEAR(zRange(up, false), 4.0, 1e-12);
    FK_CHECK_NEAR(zRange(up, true), 6.0, 1e-12);
    options.side = ThickenSide::Backward;
    const Body down = thickenSheet(box, {top}, options);
    checkSolid(down);
    FK_CHECK_NEAR(massProperties(down).volume, 120.0, 1e-9);
    FK_CHECK_NEAR(zRange(down, false), 2.0, 1e-12);
    options.side = ThickenSide::Both;
    const Body both = thickenSheet(box, {top}, options);
    checkSolid(both);
    FK_CHECK_NEAR(massProperties(both).volume, 120.0, 1e-9);
    FK_CHECK_NEAR(zRange(both, false), 3.0, 1e-12);
    FK_CHECK_NEAR(zRange(both, true), 5.0, 1e-12);
    options.thickness = 0.0;
    FK_CHECK_THROWS(thickenSheet(box, {top}, options));
    // Tutte le facce della scatola: superficie chiusa, niente bordi liberi.
    options.thickness = 1.0;
    FK_CHECK_THROWS(thickenSheet(box, {}, options));
}

// Superficie curva lungo la normale: settore di anello, V = theta R t L (meta' per parte).
FK_TEST(ThickenCylindricalSheetAlongNormal) {
    const double R = 5.0, L = 8.0, t = 0.6, a = 0.3, b = 2.1;
    const Body sheet = arcSheet(R, a, b, L);
    ThickenOptions options;
    options.thickness = t;
    options.side = ThickenSide::Both;
    const Body solid = thickenSheet(sheet, {}, options);
    checkSolid(solid);
    FK_CHECK_NEAR(massProperties(solid).volume, (b - a) * R * t * L, 1e-7);
    // Un lato solo: anello tra R e R + t o tra R - t e R, secondo il verso della normale.
    options.side = ThickenSide::Forward;
    const double forward = massProperties(thickenSheet(sheet, {}, options)).volume;
    options.side = ThickenSide::Backward;
    const double backward = massProperties(thickenSheet(sheet, {}, options)).volume;
    const double outer = 0.5 * (b - a) * ((R + t) * (R + t) - R * R) * L, inner = 0.5 * (b - a) * (R * R - (R - t) * (R - t)) * L;
    FK_CHECK(std::fabs(forward - outer) < 1e-7 || std::fabs(forward - inner) < 1e-7);
    FK_CHECK_NEAR(forward + backward, outer + inner, 1e-7);
    // Verso l'asse oltre il raggio: la superficie a distanza degenera.
    options.thickness = 2.0 * R;
    options.side = ThickenSide::Both;
    FK_CHECK_THROWS(thickenSheet(sheet, {}, options));
}

// Lungo una direzione: volume = area proiettata sul piano normale alla direzione x spessore.
FK_TEST(ThickenAlongDirection) {
    const double R = 5.0, L = 8.0, t = 1.5;
    const Body cap = arcSheet(R, M_PI / 6.0, 5.0 * M_PI / 6.0, L);  // calotta: n . Z lontano da zero
    ThickenOptions options;
    options.thickness = t;
    options.useDirection = true;
    options.direction = Vec3(0.0, 0.0, 1.0);
    const Body solid = thickenSheet(cap, {}, options);
    checkSolid(solid);
    FK_CHECK_NEAR(massProperties(solid).volume, L * R * std::sqrt(3.0) * t, 1e-7);
    options.side = ThickenSide::Both;
    const Body both = thickenSheet(cap, {}, options);
    checkSolid(both);
    FK_CHECK_NEAR(massProperties(both).volume, L * R * std::sqrt(3.0) * t, 1e-7);
    // Direzione obliqua: conta solo la componente lungo la normale media.
    options.side = ThickenSide::Forward;
    options.direction = Vec3(0.3, 0.0, 1.0);
    FK_CHECK_NEAR(massProperties(thickenSheet(cap, {}, options)).volume, L * R * std::sqrt(3.0) * t / std::sqrt(1.09), 1e-7);
    // Oltre i fianchi la superficie si ripiega rispetto a Z: errore.
    const Body folded = arcSheet(R, -0.2, M_PI + 0.2, L);
    options.direction = Vec3(0.0, 0.0, 1.0);
    FK_CHECK_THROWS(thickenSheet(folded, {}, options));
}

// Spigolo vivo: lungo la normale e' rifiutato, lungo una direzione che vede
// entrambe le facce funziona (V = somma delle aree proiettate x spessore).
FK_TEST(ThickenSharpCreaseAlongDirection) {
    const Body box = makeBox(Frame3(), 10.0, 6.0, 4.0);
    const FaceId top = boxFace(box, Vec3(0.0, 0.0, 1.0)), front = boxFace(box, Vec3(0.0, -1.0, 0.0));
    FK_CHECK(top.valid() && front.valid());
    ThickenOptions options;
    options.thickness = 1.0;
    FK_CHECK_THROWS(thickenSheet(box, {top, front}, options));
    options.useDirection = true;
    options.direction = Vec3(0.0, -1.0, 1.0);
    const Body solid = thickenSheet(box, {top, front}, options);
    checkSolid(solid);
    FK_CHECK_NEAR(massProperties(solid).volume, (60.0 + 40.0) / std::sqrt(2.0), 1e-9);
}

// Faccia rifilata (un foro) su una B-spline con una piega di 0,5 gradi lungo
// un nodo C0 (come lo sweep di Mouse.prt lungo una spline solo C1): l'offset
// divide la faccia lungo l'isoparametrica della piega e lo spessore lungo la
// normale si chiude in un solido (pieghe fino a 2 gradi assorbite nella cucitura).
FK_TEST(ThickenTrimmedFaceWithInternalCrease) {
    const double rise = 5.0 * std::tan(0.5 * M_PI / 180.0);
    const std::vector<Vec3> poles{{0, 0, 0}, {0, 5, 0}, {0, 10, rise}, {10, 0, 0}, {10, 5, 0}, {10, 10, rise}};
    const auto surface = std::make_shared<BSplineSurface>(1, 1, std::vector<double>{0, 0, 1, 1}, std::vector<double>{0, 0, 1, 2, 2}, 2, 3, poles);
    const std::vector<Vec3> corners{surface->point(0, 0), surface->point(1, 0), surface->point(1, 2), surface->point(0, 2)};
    const std::vector<Body::BuildEdge> edges{{0, 1, surface->vIso(0.0), {0, 1}, 0}, {1, 2, surface->uIso(1.0), {0, 2}, 0},
                                             {3, 2, surface->vIso(2.0), {0, 1}, 0}, {0, 3, surface->uIso(0.0), {0, 2}, 0}};
    Body::BuildFace face;
    face.surface = surface;
    face.loops = {{{0, true, std::make_shared<Line<2>>(Vec2(0, 0), Vec2(1, 0)), 0}, {1, true, std::make_shared<Line<2>>(Vec2(1, 0), Vec2(0, 1)), 0},
                   {2, false, std::make_shared<Line<2>>(Vec2(0, 2), Vec2(1, 0)), 0}, {3, false, std::make_shared<Line<2>>(Vec2(0, 0), Vec2(0, 1)), 0}}};
    const Body plate = Body::buildSheet(corners, edges, {face});
    // Foro cilindrico lontano dalla piega: la faccia non e' piu' il rettangolo (u, v).
    const Body drill = makeCylinder(Frame3(Vec3(3.0, 2.5, -5.0), Vec3(0.0, 0.0, 1.0), Vec3(1.0, 0.0, 0.0)), 1.2, 10.0);
    const Body sheet = trimSheet(plate, drill, Vec3(8.0, 8.0, 0.1));
    FK_CHECK(sheet.faces().size() == 1);
    double area = 0.0;
    for (FaceId f : sheet.faces()) area += faceArea(sheet, f, 1e-11);
    // L'offset (anche senza unire le pieghe) divide la faccia sulla piega invece di fallire.
    const OffsetResult offset = offsetFaces(sheet, sheet.faces(), 0.4);
    FK_CHECK(offset.body.faces().size() == 2);
    ThickenOptions options;
    options.thickness = 0.4;
    options.side = ThickenSide::Both;
    const Body solid = thickenSheet(sheet, {}, options);
    checkSolid(solid);
    FK_CHECK(std::fabs(massProperties(solid).volume - area * 0.4) < 1e-3 * area * 0.4);
}
