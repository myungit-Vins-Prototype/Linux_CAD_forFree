#include <cmath>

#include "fk_body_check.h"
#include "fk_boolean.h"
#include "fk_curve_algo.h"
#include "fk_extrude.h"
#include "fk_mass.h"
#include "fk_primitives.h"
#include "fk_profile.h"
#include "fk_sheet.h"
#include "fk_tessellate.h"
#include "fk_test_profiles.h"

using namespace fktest;

namespace {

double sheetArea(const Body &body) {
    double area = 0.0;
    for (FaceId f : body.faces()) area += faceArea(body, f);
    return area;
}

// Lamina valida e visualizzabile, con l'area attesa (se > 0). Restituisce l'area.
double checkedSheet(const Body &body, double expected, double relative = 1e-10) {
    FK_CHECK(body.isSheet());
    for (const CheckIssue &issue : checkBody(body)) reportFailure(__FILE__, __LINE__, describe(issue.code) + ": " + issue.message);
    const double area = sheetArea(body);
    if (expected > 0.0) FK_CHECK_NEAR(area, expected, relative * expected);
    TessellationOptions options;
    options.deflection = 0.01;
    FK_CHECK(tessellate(body, options).failedFaces == 0);
    return area;
}

// Lamina estrusa lungo Z da una catena aperta di tratti nel piano XY.
Body sheetOf(const std::vector<ProfileSegment> &segments, double height, double z = 0.0) {
    const Profile profile = buildProfile(segments, 1e-9);
    return makeSheetExtrusion(Frame3(Vec3(0, 0, z), Vec3(0, 0, 1), Vec3(1, 0, 0)), profile.chains, height);
}

EdgeId edgeNear(const Body &body, const Vec3 &p) {
    EdgeId best;
    double closest = 1e300;
    for (EdgeId e : body.edges()) {
        const Edge &edge = body.edge(e);
        const double d = projectPoint(*edge.curve, p, edge.range).distance;
        if (d < closest) closest = d, best = e;
    }
    return best;
}

std::shared_ptr<BSplineCurve<2>> openSpline() {
    return std::make_shared<BSplineCurve<2>>(3, std::vector<double>{0, 0, 0, 0, 0.5, 1, 1, 1, 1},
                                             std::vector<Vec2>{Vec2(0, 0), Vec2(1, 2), Vec2(3, -1), Vec2(4, 1), Vec2(6, 0)});
}

}

FK_TEST(SheetExtendBSplineKeepsCurve) {
    // La curva prolungata coincide con l'originale sul suo dominio (anche razionale).
    for (bool rational : {false, true}) {
        const BSplineCurve<3> curve(3, {0, 0, 0, 0, 0.3, 0.7, 1, 1, 1, 1}, {Vec3(0, 0, 0), Vec3(1, 2, 0), Vec3(2, -1, 1), Vec3(3, 1, 0), Vec3(4, 0, 2), Vec3(5, 1, 1)},
                                    rational ? std::vector<double>{1, 0.8, 1.3, 0.9, 1.1, 1} : std::vector<double>{});
        // Sulle NURBS il polinomio dei pesi, prolungato, prima o poi si annulla: li' non si va.
        const double lo = rational ? -0.1 : -0.4, hi = rational ? 1.1 : 1.5;
        const BSplineCurve<3> longer = extendBSpline(curve, lo, hi);
        FK_CHECK_NEAR(longer.domain().lo, lo, 0.0);
        FK_CHECK_NEAR(longer.domain().hi, hi, 0.0);
        double worst = 0.0;
        for (double t = 0.0; t <= 1.0; t += 0.01) worst = std::max(worst, distance(curve.point(t), longer.point(t)));
        // Fuori dal dominio: lo stesso polinomio con cui la curva di partenza si prolunga.
        for (double t : {lo, 0.5 * lo, 1.0 + 0.5 * (hi - 1.0), hi}) worst = std::max(worst, distance(curve.point(t), longer.point(t)));
        if (rational) FK_CHECK_THROWS(extendBSpline(curve, -0.4, 1.5));
        FK_CHECK(worst < 1e-12);
    }
}

FK_TEST(SheetSplitAndTrim) {
    // Piano 20 x 20 tagliato da un cilindro (solido): disco e lastra forata.
    const Body plate = makePlaneSheet(Frame3(), 10.0);
    const Body rod = makeCylinder(Frame3(Vec3(1, 2, -3), Vec3(0, 0, 1), Vec3(1, 0, 0)), 2.5, 6.0);
    const std::vector<Body> pieces = splitSheet(plate, rod);
    FK_CHECK(pieces.size() == 2);
    double total = 0.0;
    for (const Body &piece : pieces) total += checkedSheet(piece, 0.0);
    FK_CHECK_NEAR(total, 400.0, 1e-10 * 400.0);
    checkedSheet(trimSheet(plate, rod, Vec3(8, 8, 0)), 400.0 - kPi * 2.5 * 2.5);
    checkedSheet(trimSheet(plate, rod, Vec3(1, 2, 0)), kPi * 2.5 * 2.5);
    // Lamina con lamina: il piano diviso da un piano verticale (x = 2).
    const Body wall = makePlaneSheet(Frame3(Vec3(2, 0, 0), Vec3(1, 0, 0), Vec3(0, 1, 0)), 30.0);
    checkedSheet(trimSheet(plate, wall, Vec3(-5, 0, 0)), 12.0 * 20.0);
    checkedSheet(trimSheet(plate, wall, Vec3(6, 0, 0)), 8.0 * 20.0);
    // Una spline estrusa (alta 3) tagliata da un piano orizzontale a quota 1.
    const auto spline = openSpline();
    const Body curtain = sheetOf({{spline, spline->domain()}}, 3.0);
    const double L = arcLength(*spline, spline->domain());
    const Body level = makePlaneSheet(Frame3(Vec3(3, 0, 1), Vec3(0, 0, 1), Vec3(1, 0, 0)), 20.0);
    checkedSheet(trimSheet(curtain, level, Vec3(3, 0.2, 0.2)), L * 1.0, 1e-9);
    checkedSheet(trimSheet(curtain, level, Vec3(3, 0.2, 2.5)), L * 2.0, 1e-9);
    // Lamina piegata (due facce) e cilindro con l'asse sullo spigolo comune: i
    // pezzi fuori dal cilindro restano separati (l'angolo che li univa e' tolto).
    const Body folded = sheetOf({lineSegment(Vec2(-6, 0), Vec2(-1, 0)), lineSegment(Vec2(-1, 0), Vec2(-1, 4))}, 2.0);
    const Body post = makeCylinder(Frame3(Vec3(-1, 0, -1), Vec3(0, 0, 1), Vec3(1, 0, 0)), 1.0, 4.0);
    FK_CHECK(splitSheet(folded, post).size() == 3);
    checkedSheet(trimSheet(folded, post, Vec3(-5, 0, 1)), (5.0 - 1.0) * 2.0);
    checkedSheet(trimSheet(folded, post, Vec3(-1, 3, 1)), (4.0 - 1.0) * 2.0);
    checkedSheet(trimSheet(folded, post, Vec3(-1.5, 0, 1)), 2.0 * 2.0);
    // Uno strumento che non divide la lamina.
    FK_CHECK_THROWS(trimSheet(plate, makeCylinder(Frame3(Vec3(50, 0, -1), Vec3(0, 0, 1), Vec3(1, 0, 0)), 1.0, 2.0), Vec3(0, 0, 0)));
}

FK_TEST(SheetExtend) {
    const double h = 3.0, d = 1.25;
    // Piano: bordo in alto e bordo all'estremo (la faccia resta una).
    const Body flat = sheetOf({lineSegment(Vec2(0, 0), Vec2(4, 0))}, h);
    const Body taller = extendSheet(flat, {edgeNear(flat, Vec3(2, 0, h))}, d);
    checkedSheet(taller, 4.0 * (h + d));
    FK_CHECK(taller.faces().size() == 1);
    const Body wider = extendSheet(flat, {edgeNear(flat, Vec3(4, 0, 1))}, d);
    checkedSheet(wider, (4.0 + d) * h);
    FK_CHECK(wider.faces().size() == 1);
    // Cilindro (arco di 90 gradi): in alto, e all'estremo lungo l'arco o tangente (piano).
    const double R = 2.0;
    const Body arc = sheetOf({arcSegment(Vec2(0, 0), R, 0.0, kPi / 2)}, h);
    checkedSheet(extendSheet(arc, {edgeNear(arc, Vec3(R * std::sqrt(0.5), R * std::sqrt(0.5), 0.0))}, d), R * kPi / 2 * (h + d));
    const Body around = extendSheet(arc, {edgeNear(arc, Vec3(R, 0, 1))}, d);
    checkedSheet(around, (R * kPi / 2 + d) * h);
    FK_CHECK(around.faces().size() == 1);
    const Body tangent = extendSheet(arc, {edgeNear(arc, Vec3(R, 0, 1))}, d, true);
    checkedSheet(tangent, (R * kPi / 2 + d) * h);
    FK_CHECK(tangent.faces().size() == 2);
    // Spline: all'estremo il polinomio prosegue (una faccia sola), in alto la striscia.
    const auto spline = openSpline();
    const double L = arcLength(*spline, spline->domain());
    const Body curtain = sheetOf({{spline, spline->domain()}}, h);
    const Vec3 end(6, 0, 1), start(0, 0, 1);
    const Body longer = extendSheet(curtain, {edgeNear(curtain, end)}, d);
    checkedSheet(longer, (L + d) * h, 1e-9);
    FK_CHECK(longer.faces().size() == 1);
    checkedSheet(extendSheet(curtain, {edgeNear(curtain, end), edgeNear(curtain, start)}, d), (L + 2.0 * d) * h, 1e-9);
    checkedSheet(extendSheet(curtain, {edgeNear(curtain, end)}, d, true), (L + d) * h, 1e-9);
    checkedSheet(extendSheet(curtain, {edgeNear(curtain, Vec3(3, 0.2, h))}, d), L * (h + d), 1e-9);
    // Lamina a piu' facce: tutti i bordi in alto insieme (le strisce si uniscono sugli spigoli comuni).
    const Body folded = sheetOf({lineSegment(Vec2(-6, 0), Vec2(0, 0)), arcSegment(Vec2(0, 2), 2.0, -kPi / 2, 0.0), lineSegment(Vec2(2, 2), Vec2(2, 5))}, h);
    const double lengthFolded = 6.0 + kPi + 3.0;
    std::vector<EdgeId> tops;
    for (EdgeId e : folded.edges()) {
        const Edge &edge = folded.edge(e);
        if (std::fabs(edge.curve->point(0.5 * (edge.range.lo + edge.range.hi)).z() - h) < 1e-9 && folded.isLaminar(e)) tops.push_back(e);
    }
    FK_CHECK(tops.size() == 3);
    const Body raised = extendSheet(folded, tops, d);
    checkedSheet(raised, lengthFolded * (h + d), 1e-9);
    FK_CHECK(raised.faces().size() == folded.faces().size());
    // Bordi non isoparametrici e bordi interni non si estendono.
    FK_CHECK_THROWS(extendSheet(folded, {edgeNear(folded, Vec3(0, 0, 1))}, d));
}
