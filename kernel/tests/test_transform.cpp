#include <cmath>

#include "fk_blend.h"
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
