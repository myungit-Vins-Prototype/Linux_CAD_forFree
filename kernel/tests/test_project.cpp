#include <cmath>

#include "fk_body_check.h"
#include "fk_mass.h"
#include "fk_primitives.h"
#include "fk_project.h"
#include "fk_sew.h"
#include "fk_surface_algo.h"
#include "fk_tessellate.h"
#include "fk_test.h"

using namespace ForgeCad::Kernel;

namespace {

ProfileSegment segment2(const Vec2 &a, const Vec2 &b) { return {std::make_shared<Line<2>>(a, b - a), {0.0, distance(a, b)}}; }

// Rettangolo [x0, x1] x [y0, y1] nel piano del profilo.
Profile rectangle(double x0, double y0, double x1, double y1) {
    const Vec2 a(x0, y0), b(x1, y0), c(x1, y1), d(x0, y1);
    return buildProfile({segment2(a, b), segment2(b, c), segment2(c, d), segment2(d, a)}, 1e-9);
}

double totalArea(const Body &body) {
    double area = 0.0;
    for (FaceId f : body.faces()) area += faceArea(body, f, 1e-11);
    return area;
}

void checkValid(const Body &body) {
    for (const CheckIssue &issue : checkBody(body)) fktest::reportFailure(__FILE__, __LINE__, describe(issue.code) + ": " + issue.message);
    TessellationOptions options;
    options.deflection = 1e-3;
    FK_CHECK(tessellate(body, options).failedFaces == 0);
}

// Scatola [0, 10] x [0, 6] x [0, 4] come lamina (tutte le facce).
Body boxSheet() {
    const Body box = makeBox(Frame3(), 10.0, 6.0, 4.0);
    return facesAsSheet(box, box.faces());
}

// Piano del profilo sopra la scatola, normale +Z: si proietta verso -Z.
const Frame3 kAbove(Vec3(0.0, 0.0, 9.0), Vec3(0.0, 0.0, 1.0), Vec3(1.0, 0.0, 0.0));

}

// Foro rettangolare nella faccia superiore: le pareti sotto la regione non si toccano.
FK_TEST(ProjectedCutPlanarHole) {
    const Body sheet = boxSheet();
    const double area0 = totalArea(sheet);
    const Profile profile = rectangle(2.0, 1.5, 7.0, 4.0);
    ProjectedCutReport report;
    const Body cut = projectedProfileCut(sheet, kAbove, profile.regions, {}, Vec3(0.0, 0.0, -1.0), ProjectedCutMode::RemoveInside, &report);
    checkValid(cut);
    FK_CHECK(cut.isSheet());
    FK_CHECK_NEAR(totalArea(cut), area0 - 12.5, 1e-9);
    FK_CHECK(cut.faces().size() == 6);
    FK_CHECK(report.cuts == 4 && report.removed == 1 && report.followedEdges == 0);

    const Body inside = projectedProfileCut(sheet, kAbove, profile.regions, {}, Vec3(0.0, 0.0, -1.0), ProjectedCutMode::KeepInside);
    checkValid(inside);
    FK_CHECK(inside.faces().size() == 1);
    FK_CHECK_NEAR(totalArea(inside), 12.5, 1e-9);

    const Body split = projectedProfileCut(sheet, kAbove, profile.regions, {}, Vec3(0.0, 0.0, -1.0), ProjectedCutMode::SplitOnly);
    checkValid(split);
    FK_CHECK(split.faces().size() == 7);
    FK_CHECK_NEAR(totalArea(split), area0, 1e-9);
    // Un solido diviso resta un solido.
    const Body solid = makeBox(Frame3(), 10.0, 6.0, 4.0);
    const Body solidSplit = projectedProfileCut(solid, kAbove, profile.regions, {}, Vec3(0.0, 0.0, -1.0), ProjectedCutMode::SplitOnly);
    checkValid(solidSplit);
    FK_CHECK(!solidSplit.isSheet());
    FK_CHECK_NEAR(massProperties(solidSplit).volume, 240.0, 1e-9);
}

// Fascia lungo un bordo (come lo Schizzo 4 di Mouse.prt): tre lati coincidono
// con gli spigoli della faccia superiore e non creano tagli, uno solo divide.
FK_TEST(ProjectedCutBandAlongExistingEdges) {
    const Body sheet = boxSheet();
    const double area0 = totalArea(sheet);
    const Profile profile = rectangle(0.0, 0.0, 10.0, 0.5);
    ProjectedCutReport report;
    const Body cut = projectedProfileCut(sheet, kAbove, profile.regions, {}, Vec3(0.0, 0.0, -1.0), ProjectedCutMode::RemoveInside, &report);
    checkValid(cut);
    FK_CHECK(report.cuts == 1);
    FK_CHECK(report.followedEdges == 3);
    FK_CHECK(report.removed == 1);
    FK_CHECK_NEAR(totalArea(cut), area0 - 5.0, 1e-9);
    // Le pareti restano intere: il fronte y = 0 ora ha un bordo libero in alto.
    FK_CHECK(cut.faces().size() == 6);
}

// Corda di un bordo curvo: il segmento scosta di poco dal bordo (meno di
// followTolerance) e lo segue.
FK_TEST(ProjectedCutChordOfCurvedEdge) {
    // Cilindro di raggio 3 con l'asse Z: la faccia superiore e' un disco.
    const Body solid = makeCylinder(Frame3(), 3.0, 2.0);
    const Body sheet = facesAsSheet(solid, solid.faces());
    const double area0 = totalArea(sheet);
    // Settore circolare sottile: arco del bordo sostituito dalla corda (freccia 3 (1 - cos(0.01)) = 1.5e-4).
    const double a = 0.01;
    const Vec2 p(3.0 * std::cos(-a), 3.0 * std::sin(-a)), q(3.0 * std::cos(a), 3.0 * std::sin(a));
    const Vec2 p1 = 2.0 / 3.0 * p, q1 = 2.0 / 3.0 * q;
    const Profile profile = buildProfile({segment2(p1, p), segment2(p, q), segment2(q, q1), segment2(q1, p1)}, 1e-9);
    const Frame3 above(Vec3(0.0, 0.0, 7.0), Vec3(0.0, 0.0, 1.0), Vec3(1.0, 0.0, 0.0));
    ProjectedCutReport report;
    const Body cut = projectedProfileCut(sheet, above, profile.regions, {}, Vec3(0.0, 0.0, -1.0), ProjectedCutMode::RemoveInside, &report);
    checkValid(cut);
    FK_CHECK(report.followedEdges == 1);
    FK_CHECK(report.followedGap > 1e-4 && report.followedGap < 2e-4);
    // Tolto: il settore tra la corda interna e l'arco del bordo.
    const double removed = 9.0 * a - 0.5 * 4.0 * std::sin(2.0 * a);
    FK_CHECK_NEAR(totalArea(cut), area0 - removed, 1e-8);
    // Con una tolleranza piu' stretta della freccia la corda e' un taglio vero:
    // resta la lunetta tra la corda e l'arco (fuori dalla regione).
    ProjectionOptions strict;
    strict.followTolerance = 1e-5;
    ProjectedCutReport strictReport;
    const Body exact = projectedProfileCut(sheet, above, profile.regions, {}, Vec3(0.0, 0.0, -1.0), ProjectedCutMode::RemoveInside, &strictReport, strict);
    checkValid(exact);
    FK_CHECK(strictReport.followedEdges == 0 && strictReport.cuts == 4);
    const double lune = 4.5 * (2.0 * a - std::sin(2.0 * a));
    FK_CHECK_NEAR(totalArea(exact), area0 - removed + lune, 1e-8);
}

// Superficie curva: cilindro orizzontale (asse X, raggio R). La proiezione
// dall'alto toglie solo la parte superiore: la stessa faccia periodica passa
// anche sotto la regione, ma li' non e' la prima colpita.
FK_TEST(ProjectedCutOnCylinderFirstFaceOnly) {
    const double R = 3.0, L = 10.0;
    const Body solid = makeCylinder(Frame3(Vec3(), Vec3(1.0, 0.0, 0.0), Vec3(0.0, 1.0, 0.0)), R, L);
    std::vector<FaceId> side;
    for (FaceId f : solid.faces())
        if (solid.face(f).surface->type() == SurfaceType::Cylinder) side.push_back(f);
    const Body sheet = facesAsSheet(solid, side);
    const double area0 = totalArea(sheet);
    FK_CHECK_NEAR(area0, 2.0 * M_PI * R * L, 1e-9);
    const double x0 = 2.0, x1 = 7.0, c = 1.2;
    const Profile profile = rectangle(x0, -c, x1, c);
    ProjectedCutReport report;
    const Body cut = projectedProfileCut(sheet, kAbove, profile.regions, {}, Vec3(0.0, 0.0, -1.0), ProjectedCutMode::RemoveInside, &report);
    checkValid(cut);
    FK_CHECK(report.removed == 1);
    FK_CHECK(report.deviation <= 1e-8);
    FK_CHECK_NEAR(totalArea(cut), area0 - (x1 - x0) * 2.0 * R * std::asin(c / R), 1e-7);
    // Verso opposto (dal basso): la stessa area, tolta sotto.
    const Frame3 below(Vec3(0.0, 0.0, -9.0), Vec3(0.0, 0.0, 1.0), Vec3(1.0, 0.0, 0.0));
    const Body cutBelow = projectedProfileCut(sheet, below, profile.regions, {}, Vec3(0.0, 0.0, 1.0), ProjectedCutMode::RemoveInside);
    checkValid(cutBelow);
    FK_CHECK_NEAR(totalArea(cutBelow), area0 - (x1 - x0) * 2.0 * R * std::asin(c / R), 1e-7);
    // Una regione che non incontra il corpo: errore.
    const Profile away = rectangle(20.0, 20.0, 25.0, 25.0);
    FK_CHECK_THROWS(projectedProfileCut(sheet, kAbove, away.regions, {}, Vec3(0.0, 0.0, -1.0), ProjectedCutMode::RemoveInside));
}

// Curve proiettate: un cerchio sopra il cilindro orizzontale diventa una
// curva chiusa sulla superficie (punti a distanza R dall'asse, x e y del cerchio).
FK_TEST(ProjectedCurveClosedOnCylinder) {
    const double R = 3.0, L = 10.0;
    const Body solid = makeCylinder(Frame3(Vec3(), Vec3(1.0, 0.0, 0.0), Vec3(0.0, 1.0, 0.0)), R, L);
    const auto circle = std::make_shared<Circle<3>>(Vec3(5.0, 0.0, 9.0), Vec3(1.0, 0.0, 0.0), Vec3(0.0, 1.0, 0.0), 2.0);
    const std::vector<ProjectedPiece> pieces = projectCurve(solid, *circle, circle->domain(), Vec3(0.0, 0.0, -1.0));
    FK_CHECK(!pieces.empty());
    for (const ProjectedPiece &piece : pieces) FK_CHECK(piece.deviation <= 1e-8 && !piece.onEdge);
    const std::vector<ProjectedChain> chains = joinProjectedPieces(pieces);
    FK_CHECK(chains.size() == 1);
    FK_CHECK(chains.front().closed);
    const BSplineCurve<3> &curve = *chains.front().curve;
    const Interval domain = curve.domain();
    double worst = 0.0;
    for (int k = 0; k <= 200; ++k) {
        const Vec3 p = curve.point(domain.lo + domain.length() * k / 200.0);
        worst = std::max(worst, std::fabs(std::hypot(p.y(), p.z()) - R));
        // Sul cerchio in pianta.
        worst = std::max(worst, std::fabs(std::hypot(p.x() - 5.0, p.y()) - 2.0));
        FK_CHECK(p.z() > 0.0);
    }
    FK_CHECK(worst <= 1e-7);
}
