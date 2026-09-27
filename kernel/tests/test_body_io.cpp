#include <cmath>
#include <stdexcept>

#include "fk_body_check.h"
#include "fk_body_io.h"
#include "fk_boolean.h"
#include "fk_extrude.h"
#include "fk_helix.h"
#include "fk_mass.h"
#include "fk_primitives.h"
#include "fk_revolve.h"
#include "fk_sweep.h"
#include "fk_test_profiles.h"
#include "fk_transform.h"

using namespace fktest;

namespace {

// Il body riletto e' valido e uguale all'originale: stesse entita', stessi
// punti, tratti e tolleranze, stesse proprieta' di massa.
void checkRoundTrip(const Body &body) {
    Body copy;
    try {
        copy = readBodyBinary(writeBodyBinary(body));
    } catch (const std::exception &error) {
        reportFailure(__FILE__, __LINE__, error.what());
        return;
    }
    for (const CheckIssue &issue : checkBody(copy)) reportFailure(__FILE__, __LINE__, describe(issue.code) + ": " + issue.message);
    const TopologyCounts a = body.counts(), b = copy.counts();
    FK_CHECK(a.vertices == b.vertices && a.edges == b.edges && a.fins == b.fins && a.loops == b.loops && a.faces == b.faces
             && a.shells == b.shells && a.regions == b.regions);
    const std::vector<VertexId> va = body.vertices(), vb = copy.vertices();
    if (va.size() == vb.size())
        for (std::size_t i = 0; i < va.size(); ++i) {
            FK_CHECK(va[i] == vb[i]);
            FK_CHECK(body.vertex(va[i]).point[0] == copy.vertex(vb[i]).point[0] && body.vertex(va[i]).point[2] == copy.vertex(vb[i]).point[2]);
            FK_CHECK(body.vertex(va[i]).tolerance == copy.vertex(vb[i]).tolerance);
        }
    const std::vector<EdgeId> ea = body.edges(), eb = copy.edges();
    if (ea.size() == eb.size())
        for (std::size_t i = 0; i < ea.size(); ++i) {
            const Edge &x = body.edge(ea[i]), &y = copy.edge(eb[i]);
            FK_CHECK(x.range.lo == y.range.lo && x.range.hi == y.range.hi && x.tolerance == y.tolerance);
            FK_CHECK(distance(x.curve->point(x.range.lo), y.curve->point(y.range.lo)) <= 1e-14 * (1.0 + norm(x.curve->point(x.range.lo))));
        }
    FK_CHECK(body.isSheet() == copy.isSheet());
    if (!body.isSheet()) {
        const MassProperties m = massProperties(body), n = massProperties(copy);
        FK_CHECK_NEAR(n.volume, m.volume, 1e-13 * std::fabs(m.volume));
        FK_CHECK_NEAR(n.area, m.area, 1e-13 * m.area);
    }
}

}

FK_TEST(BodyIORoundTrip) {
    // Estrusioni (segmenti, archi, spline chiusa: superficie estrusa), primitive di rivoluzione.
    checkRoundTrip(makeExtrusion(Frame3(), buildProfile(roundedRectangle(Vec2(0, 0), 6, 4, 1), 1e-9).regions.front(), 2.0));
    checkRoundTrip(makeExtrusion(Frame3(Vec3(1, 2, 3), Vec3(0, 1, 1), Vec3(1, 0, 0)), buildProfile({closedSpline(Vec2(0, 0), 2.0, true)}, 1e-9).regions.front(), 1.5));
    checkRoundTrip(makeSphere(Frame3(Vec3(2, 0, 1), Vec3(0, 0, 1), Vec3(1, 0, 0)), 2.0));
    checkRoundTrip(makeCone(Frame3(), 2.0, 0.5, 3.0));
    checkRoundTrip(makeTorus(Frame3(), 3.0, 1.0));
    // Booleana tra cilindri (curve del marching con le loro SP-curve), poi spostata e specchiata.
    const Body main = makeCylinder(Frame3(Vec3(-5, 0, 0), Vec3(1, 0, 0), Vec3(0, 1, 0)), 2.0, 10.0);
    const Body tee = booleanOperation(main, makeCylinder(Frame3(Vec3(0, 0, 0), Vec3(0, 0, 1), Vec3(1, 0, 0)), 1.0, 4.0), BooleanOperation::Unite);
    checkRoundTrip(tee);
    checkRoundTrip(transformBody(tee, Transform3::translation(Vec3(3, 1, -2)) * Transform3::rotation(Vec3(0, 0, 0), normalized(Vec3(1, 1, 0)), 0.7)));
    checkRoundTrip(mirrorBody(tee, Vec3(0, 0, 1), Vec3(0, 0, 1)));
    // Sweep di un cerchio lungo un'elica (facce B-spline) e lamina.
    HelixSpec spec;
    spec.frame = Frame3(Vec3(0, 0, 0), Vec3(0, 0, 1), Vec3(1, 0, 0));
    spec.radius = 4.0;
    spec.pitch = 1.5;
    spec.turns = 0.5;
    const auto helix = std::make_shared<HelixCurve>(spec);
    ProfileRegion circle;
    circle.outer.segments = {arcSegment(Vec2(0, 0), 0.5, 0.0, kTwoPi)};
    const Frame3 start(helix->point(0.0), normalized(helix->derivative(0.0)), Vec3(0.3, 0.7, 0.2));
    checkRoundTrip(sweepRegions(start, {circle}, {{helix, helix->domain()}}));
    checkRoundTrip(makeSheetExtrusion(Frame3(), buildProfile({lineSegment(Vec2(0, 0), Vec2(2, 0)), lineSegment(Vec2(2, 0), Vec2(2, 1))}, 1e-9).chains, 1.0));
}

FK_TEST(BodyIORejectsBadData) {
    const std::string data = writeBodyBinary(makeCylinder(Frame3(), 1.0, 2.0));
    FK_CHECK_THROWS(readBodyBinary(data.substr(0, data.size() / 2)));
    FK_CHECK_THROWS(readBodyBinary("FKBD"));
    std::string other = data;
    other[0] = 'X';
    FK_CHECK_THROWS(readBodyBinary(other));
    FK_CHECK_THROWS(readBodyBinary(data + "x"));
}
