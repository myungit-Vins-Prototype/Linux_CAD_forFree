#include <cmath>

#include "fk_blend.h"
#include "fk_body_check.h"
#include "fk_boolean.h"
#include "fk_extrude.h"
#include "fk_profile.h"
#include "fk_test_profiles.h"
#include "fk_mass.h"
#include "fk_primitives.h"
#include "fk_surface_algo.h"
#include "fk_shell.h"
#include "fk_tessellate.h"
#include "fk_test.h"

using namespace ForgeCad::Kernel;

namespace {

// La faccia del corpo che contiene il punto p.
FaceId faceAt(const Body &body, const Vec3 &p) {
    for (FaceId f : body.faces()) {
        const SurfaceProjection at = projectPoint(*body.face(f).surface, p);
        if (distance(body.face(f).surface->point(at.u, at.v), p) < 1e-9) return f;
    }
    return FaceId();
}

void checkShell(const Body &solid, const std::vector<FaceId> &removed, double thickness, double volume) {
    Body shell;
    try {
        shell = shellBody(solid, removed, thickness);
    } catch (const std::exception &error) {
        fktest::reportFailure(__FILE__, __LINE__, error.what());
        return;
    }
    for (const CheckIssue &issue : checkBody(shell)) fktest::reportFailure(__FILE__, __LINE__, describe(issue.code) + ": " + issue.message);
    FK_CHECK_NEAR(massProperties(shell).volume, volume, 1e-7 * volume);
    TessellationOptions options;
    options.deflection = 1e-3;
    FK_CHECK(tessellate(shell, options).failedFaces == 0);
}

const Frame3 kFrame(Vec3(1, -2, 0.5), Vec3(0.2, 0.1, 1.0), Vec3(1, 0, 0));

}

// Scatola 10 x 8 x 6 aperta in alto, spessore 1: cavita' 8 x 6 x 5.
FK_TEST(ShellOpenBox) {
    const Body box = makeBox(kFrame, 10.0, 8.0, 6.0);
    const FaceId top = faceAt(box, kFrame.toGlobal(Vec3(5, 4, 6)));
    FK_CHECK(top.valid());
    checkShell(box, {top}, 1.0, 480.0 - 240.0);
    // Chiusa: cavita' interna 8 x 6 x 4.
    checkShell(box, {}, 1.0, 480.0 - 192.0);
    // Aperta sopra e su un fianco.
    const FaceId side = faceAt(box, kFrame.toGlobal(Vec3(0, 4, 3)));
    FK_CHECK(side.valid());
    checkShell(box, {top, side}, 1.0, 480.0 - 9.0 * 6.0 * 5.0);
}

// Bicchiere: cilindro aperto in alto (faccia laterale curva).
FK_TEST(ShellCup) {
    const Body cylinder = makeCylinder(kFrame, 3.0, 5.0);
    const FaceId top = faceAt(cylinder, kFrame.toGlobal(Vec3(0, 0, 5)));
    FK_CHECK(top.valid());
    checkShell(cylinder, {top}, 0.5, kPi * 9.0 * 5.0 - kPi * 6.25 * 4.5);
}

// Prisma a L chiuso: lungo lo spigolo concavo l'interno ha un raccordo di
// raggio pari allo spessore (offset esatto).
FK_TEST(ShellConcaveEdge) {
    const Body block = makePrism(kFrame, {Vec2(0, 0), Vec2(4, 0), Vec2(4, 2), Vec2(2, 2), Vec2(2, 4), Vec2(0, 4)}, {}, 3.0);
    const double inner = (5.25 - kPi / 16.0) * 2.0;
    checkShell(block, {}, 0.5, 36.0 - inner);
}

// Scatola con uno spigolo verticale raccordato (raggio 1), aperta in alto,
// spessore 0.5: all'interno il raccordo diventa di raggio 0.5.
FK_TEST(ShellFilletedBox) {
    const Body box = makeBox(kFrame, 10.0, 8.0, 6.0);
    const EdgeId vertical = nearestEdge(box, kFrame.toGlobal(Vec3(10, 8, 3)), 1e-6);
    FK_CHECK(vertical.valid());
    const Body rounded = blendEdges(box, {vertical}, 1.0, false);
    const FaceId top = faceAt(rounded, kFrame.toGlobal(Vec3(5, 4, 6)));
    FK_CHECK(top.valid());
    const double outer = 480.0 - (1.0 - kPi / 4.0) * 6.0, cavity = (63.0 - (1.0 - kPi / 4.0) * 0.25) * 5.5;
    checkShell(rounded, {top}, 0.5, outer - cavity);
}

// Prisma di un rettangolo arrotondato (fianchi cilindrici a 90 gradi dalle
// basi): le fasce delle basi lungo gli archi stanno sui cilindri dei fianchi.
FK_TEST(ShellRoundedPrism) {
    const Profile profile = buildProfile(fktest::roundedRectangle(Vec2(0, 0), 6.0, 4.0, 1.0), 1e-9);
    const Body prism = makeExtrusion(kFrame, profile.regions.front(), 3.0);
    const FaceId top = faceAt(prism, kFrame.toGlobal(Vec3(3, 2, 3)));
    FK_CHECK(top.valid());
    const double outer = 24.0 - (4.0 - kPi), inner = 5.4 * 3.4 - (4.0 - kPi) * 0.49;
    checkShell(prism, {top}, 0.3, outer * 3.0 - inner * 2.7);
    checkShell(prism, {}, 0.3, outer * 3.0 - inner * 2.4);
}

// Prisma con una tacca circolare (spigoli vivi convessi tra il fondo e la
// tacca) e un foro cieco raccordato sulla faccia superiore: la faccia, il
// raccordo e la parete del foro fanno un gruppo tangente (lastra cucita).
FK_TEST(ShellNotchAndFilletedHole) {
    const double r = 0.8, c = -4.3, xs = std::sqrt(r * r - 0.09);
    // Angoli arrotondati (raggio 1.5): i fianchi sono un gruppo tangente.
    const double q = 1.5;
    const std::vector<ProfileSegment> outline{
        fktest::lineSegment(Vec2(-5, -q), Vec2(-5, -4 + q)), fktest::arcSegment(Vec2(-5 + q, -4 + q), q, kPi, 1.5 * kPi),
        fktest::lineSegment(Vec2(-5 + q, -4), Vec2(-xs, -4)), fktest::arcSegment(Vec2(0, c), r, std::acos(xs / r), kPi - std::acos(xs / r)),
        fktest::lineSegment(Vec2(xs, -4), Vec2(5 - q, -4)), fktest::arcSegment(Vec2(5 - q, -4 + q), q, 1.5 * kPi, kTwoPi),
        fktest::lineSegment(Vec2(5, -4 + q), Vec2(5, -q)), fktest::arcSegment(Vec2(5 - q, -q), q, 0.0, kHalfPi),
        fktest::lineSegment(Vec2(5 - q, 0), Vec2(-5 + q, 0)), fktest::arcSegment(Vec2(-5 + q, -q), q, kHalfPi, kPi)};
    const Profile profile = buildProfile(outline, 1e-9);
    FK_CHECK(profile.regions.size() == 1);
    if (profile.regions.size() != 1) return;
    const Frame3 frame(Vec3(0, 0, 0), Vec3(0, 0, 1), Vec3(1, 0, 0));
    Body prism = makeExtrusion(frame, profile.regions.front(), 3.0);
    const Body hole = makeCylinder(Frame3(Vec3(-2.5, -2, 2), Vec3(0, 0, 1), Vec3(1, 0, 0)), 1.0, 2.0);
    prism = booleanOperation(prism, hole, BooleanOperation::Subtract);
    const EdgeId rim = nearestEdge(prism, Vec3(-1.5, -2, 3), 1e-6);
    FK_CHECK(rim.valid());
    const Body part = blendEdges(prism, {rim}, 0.5, false);
    const FaceId bottom = faceAt(part, Vec3(0, -1, 0));
    FK_CHECK(bottom.valid());
    try {
        const Body shell = shellBody(part, {bottom}, 0.3);
        for (const CheckIssue &issue : checkBody(shell)) fktest::reportFailure(__FILE__, __LINE__, describe(issue.code) + ": " + issue.message);
        FK_CHECK(massProperties(shell).volume > 0.0);
    } catch (const std::exception &error) {
        fktest::reportFailure(__FILE__, __LINE__, error.what());
    }
}

FK_TEST(ShellRejectsBadInput) {
    const Body box = makeBox(kFrame, 1.0, 1.0, 1.0);
    FK_CHECK_THROWS(shellBody(box, {}, 0.0));
    FK_CHECK_THROWS(shellBody(box, box.faces(), 0.1));
}
