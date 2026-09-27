#include <BRepGProp.hxx>
#include <BRepOffsetAPI_ThruSections.hxx>
#include <GProp_GProps.hxx>

#include <cmath>

#include "fk_body_check.h"
#include "fk_loft.h"
#include "fk_mass.h"
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
