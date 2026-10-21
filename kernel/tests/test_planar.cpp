#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>

#include <algorithm>
#include <cmath>
#include <random>

#include "fk_body_check.h"
#include "fk_curve_ops.h"
#include "fk_extrude.h"
#include "fk_mass.h"
#include "fk_planar.h"
#include "fk_primitives.h"
#include "fk_tessellate.h"
#include "fk_test_profiles.h"

using namespace fktest;

namespace {

const Frame3 kTilted(Vec3(3, -1, 2), Vec3(0.3, -0.4, 1.0), Vec3(1, 0.2, 0));

PathSegment placed(const ProfileSegment &segment, const Frame3 &frame = kTilted) {
    return {embedCurve(segment.curve, frame, 0.0), segment.range};
}

std::vector<PathSegment> placed(const std::vector<ProfileSegment> &segments, const Frame3 &frame = kTilted) {
    std::vector<PathSegment> result;
    for (const ProfileSegment &s : segments) result.push_back(placed(s, frame));
    return result;
}

std::vector<ProfileSegment> rectangle(const Vec2 &a, double w, double h) {
    const Vec2 b = a + Vec2(w, 0), c = a + Vec2(w, h), d = a + Vec2(0, h);
    return {lineSegment(a, b), lineSegment(b, c), lineSegment(c, d), lineSegment(d, a)};
}

double totalArea(const Body &body) {
    double area = 0.0;
    for (FaceId f : body.faces()) area += faceArea(body, f);
    return area;
}

Vec3 faceNormal(const Body &body, FaceId f) {
    const Face &face = body.face(f);
    const Vec3 n = face.surface->normal(0.0, 0.0);
    return face.sense ? n : -n;
}

// Lamina valida, tassellata senza errori, con tutte le facce piane e la normale attesa.
void checkSheet(const Body &body, const Vec3 &normal) {
    FK_CHECK(body.isSheet());
    for (const CheckIssue &issue : checkBody(body)) reportFailure(__FILE__, __LINE__, describe(issue.code) + ": " + issue.message);
    TessellationOptions options;
    options.deflection = 0.005;
    const Tessellation mesh = tessellate(body, options);
    FK_CHECK(mesh.failedFaces == 0);
    for (const FaceMesh &face : mesh.faces) {
        FK_CHECK(!face.triangles.empty());
        for (const Vec3 &n : face.normals) FK_CHECK_NEAR(dot(n, normal), 1.0, 1e-12);
    }
    for (FaceId f : body.faces()) {
        FK_CHECK(body.face(f).surface->type() == SurfaceType::Plane);
        FK_CHECK_NEAR(dot(faceNormal(body, f), normal), 1.0, 1e-12);
    }
}

}

// Rettangolo con un foro circolare: una faccia con due loop, area w h - pi r^2.
FK_TEST(PlanarSheetRectangleWithHole) {
    const double w = 4.0, h = 3.0, r = 0.7;
    std::vector<std::vector<PathSegment>> loops{placed(rectangle(Vec2(0, 0), w, h)), {placed(arcSegment(Vec2(2.2, 1.4), r, 0.0, kTwoPi))}};
    const Body body = planarSheet(loops);
    checkSheet(body, kTilted.zDir());
    FK_CHECK(body.faces().size() == 1);
    FK_CHECK(body.face(body.faces()[0]).loops.size() == 2);
    FK_CHECK(body.edges().size() == 5);
    FK_CHECK_NEAR(totalArea(body), w * h - kPi * r * r, 1e-12 * w * h);
    // Il foro per primo: la normale segue il primo loop (il cerchio antiorario).
    const Body flipped = planarSheet({loops[1], loops[0]});
    checkSheet(flipped, kTilted.zDir());
    // Rettangolo orario: la normale si gira.
    std::vector<PathSegment> clockwise;
    for (const ProfileSegment &s : reversed(ProfileLoop{rectangle(Vec2(0, 0), w, h)}).segments) clockwise.push_back(placed(s));
    const Body opposite = planarSheet({clockwise, loops[1]});
    checkSheet(opposite, -kTilted.zDir());
    FK_CHECK_NEAR(totalArea(opposite), w * h - kPi * r * r, 1e-12 * w * h);
}

// Due regioni separate: due facce, area somma.
FK_TEST(PlanarSheetTwoRegions) {
    const Body body = planarSheet({placed(rectangle(Vec2(0, 0), 2, 1)), placed(rectangle(Vec2(5, 0), 1.5, 3))});
    checkSheet(body, kTilted.zDir());
    FK_CHECK(body.faces().size() == 2);
    FK_CHECK_NEAR(totalArea(body), 2.0 + 4.5, 1e-12);
}

// Tre livelli: contorno, foro, isola nel foro (profondita' pari = materiale).
FK_TEST(PlanarSheetIslandInHole) {
    const double R = 1.25;
    const Body body = planarSheet({{placed(arcSegment(Vec2(5, 5), R, 0.0, kTwoPi))}, placed(rectangle(Vec2(2, 2), 6, 6)), placed(rectangle(Vec2(0, 0), 10, 10))});
    // Il primo loop (l'isola, antioraria) da' la normale.
    checkSheet(body, kTilted.zDir());
    FK_CHECK(body.faces().size() == 2);
    std::size_t loopCount = 0;
    for (FaceId f : body.faces()) loopCount += body.face(f).loops.size();
    FK_CHECK(loopCount == 3);
    FK_CHECK_NEAR(totalArea(body), 100.0 - 36.0 + kPi * R * R, 1e-11);
}

// Il bordo superiore di un cilindro su un sistema inclinato: un cerchio 3D
// preso dal corpo, area pi r^2 e normale lungo l'asse.
FK_TEST(PlanarSheetFromCylinderEdge) {
    const Frame3 frame(Vec3(-2, 1, 0.5), Vec3(1, 2, 0.7), Vec3(0, 0, 1));
    const double r = 1.3, height = 2.5;
    const Body cylinder = makeCylinder(frame, r, height);
    const Vec3 topCenter = frame.origin() + height * frame.zDir();
    std::vector<PathSegment> top, bottom;
    for (EdgeId e : cylinder.edges()) {
        const Edge &edge = cylinder.edge(e);
        if (edge.curve->type() != CurveType::Circle) continue;
        const auto &circle = static_cast<const Circle<3> &>(*edge.curve);
        (distance(circle.center(), topCenter) < 1e-9 ? top : bottom).push_back({edge.curve, edge.range});
    }
    FK_CHECK(!top.empty() && !bottom.empty());
    const Body body = planarSheet({top});
    const Vec3 n = faceNormal(body, body.faces()[0]);
    FK_CHECK_NEAR(std::fabs(dot(n, frame.zDir())), 1.0, 1e-12);
    checkSheet(body, n);
    FK_CHECK_NEAR(totalArea(body), kPi * r * r, 1e-12);
    // Anche con gli spigoli del coperchio (i tratti come sono nel corpo).
    const Body base = planarSheet({bottom});
    checkSheet(base, faceNormal(base, base.faces()[0]));
    FK_CHECK_NEAR(totalArea(base), kPi * r * r, 1e-12);
}

// Spline chiusa (anche razionale): l'area del profilo.
FK_TEST(PlanarSheetClosedSpline) {
    for (bool rational : {false, true}) {
        const ProfileSegment spline = closedSpline(Vec2(1, -1), 2.0, rational);
        ProfileRegion region;
        region.outer.segments = {spline};
        const double expected = std::fabs(area(region));
        const Body body = planarSheet({{placed(spline)}});
        const Vec3 n = signedArea(region.outer) > 0.0 ? kTilted.zDir() : -kTilted.zDir();
        checkSheet(body, n);
        FK_CHECK(body.edges().size() == 1);
        FK_CHECK(body.edge(body.edges()[0]).curve->type() == CurveType::BSpline);
        FK_CHECK_NEAR(totalArea(body), expected, 1e-11 * expected);
    }
}

// Tratti in ordine e verso mescolati, uno staccato di poco (entro la tolleranza).
FK_TEST(PlanarSheetShuffledSegments) {
    const double w = 5.0, h = 3.0, r = 0.6;
    std::vector<ProfileSegment> profile = roundedRectangle(Vec2(-1, -1), w, h, r);
    std::vector<PathSegment> segments = placed(profile);
    for (std::size_t i = 0; i < segments.size(); i += 2) segments[i] = {reversedCurve(segments[i].curve), {-segments[i].range.hi, -segments[i].range.lo}};
    std::mt19937 rng(17);
    std::shuffle(segments.begin(), segments.end(), rng);
    const double expected = w * h - (4.0 - kPi) * r * r;
    const Body body = planarSheet({segments});
    FK_CHECK(body.edges().size() == 8);
    checkSheet(body, faceNormal(body, body.faces()[0]));
    FK_CHECK_NEAR(std::fabs(dot(faceNormal(body, body.faces()[0]), kTilted.zDir())), 1.0, 1e-12);
    FK_CHECK_NEAR(totalArea(body), expected, 1e-12 * expected);

    // Un segmento accorciato di 4e-7 nel piano: si unisce, vertice tollerante.
    std::vector<PathSegment> loose = placed(profile);
    loose[0].range.hi -= 4e-7;
    const Body tolerant = planarSheet({loose});
    checkSheet(tolerant, kTilted.zDir());
    FK_CHECK_NEAR(totalArea(tolerant), expected, 1e-6);
    // Con una tolleranza piu' stretta il loop non si chiude.
    FK_CHECK_THROWS(planarSheet({loose}, 1e-7));
}

// Curve fuori dal piano e loop aperti: errore.
FK_TEST(PlanarSheetRejectsInvalidLoops) {
    // Lato sostituito da una spline con gli stessi estremi che esce dal piano.
    std::vector<PathSegment> bent = placed(rectangle(Vec2(0, 0), 4, 2));
    const Vec3 a = bent[0].curve->point(bent[0].range.lo), b = bent[0].curve->point(bent[0].range.hi);
    auto spline = std::make_shared<BSplineCurve<3>>(2, std::vector<double>{0, 0, 0, 1, 1, 1}, std::vector<Vec3>{a, 0.5 * (a + b) + 0.01 * kTilted.zDir(), b});
    bent[0] = {spline, spline->domain()};
    FK_CHECK_THROWS(planarSheet({bent}));
    // La stessa spline nel piano va bene.
    auto flat = std::make_shared<BSplineCurve<3>>(2, std::vector<double>{0, 0, 0, 1, 1, 1}, std::vector<Vec3>{a, 0.5 * (a + b) - 0.5 * kTilted.yDir(), b});
    bent[0] = {flat, flat->domain()};
    const Body body = planarSheet({bent});
    checkSheet(body, kTilted.zDir());
    FK_CHECK_NEAR(totalArea(body), 8.0 + 2.0 / 3.0 * 4.0 * 0.25, 1e-12);  // segmento parabolico verso l'esterno: 2/3 corda per freccia
    // Cerchio inclinato rispetto al piano del rettangolo.
    const Frame3 other(kTilted.origin() + Vec3(0.5, 0.5, 0), kTilted.zDir() + 0.05 * kTilted.xDir(), kTilted.xDir());
    FK_CHECK_THROWS(planarSheet({placed(rectangle(Vec2(-1, -1), 6, 6)), {placed(arcSegment(Vec2(1, 1), 0.5, 0, kTwoPi), other)}}));
    // Cerchio in un piano parallelo spostato.
    const Frame3 shifted(kTilted.origin() + 0.01 * kTilted.zDir(), kTilted.zDir(), kTilted.xDir());
    FK_CHECK_THROWS(planarSheet({placed(rectangle(Vec2(-1, -1), 6, 6)), {placed(arcSegment(Vec2(1, 1), 0.5, 0, kTwoPi), shifted)}}));
    // Loop aperto: un lato mancante, un arco solo.
    std::vector<PathSegment> open = placed(rectangle(Vec2(0, 0), 4, 2));
    open.pop_back();
    FK_CHECK_THROWS(planarSheet({open}));
    FK_CHECK_THROWS(planarSheet({{placed(arcSegment(Vec2(0, 0), 1.0, 0.0, 3.0))}}));
    FK_CHECK_THROWS(planarSheet({}));
}

// Forma del profilo: la faccia di base di makeExtrusion (stesse curve, stessa
// area, normale opposta) e l'area di OCCT.
FK_TEST(PlanarSheetFromProfileMatchesExtrusionBase) {
    std::vector<ProfileSegment> segments = roundedRectangle(Vec2(0, 0), 6, 4, 0.8);
    segments.push_back(closedSpline(Vec2(3, 2), 1.0, true));
    segments.push_back(arcSegment(Vec2(1.2, 1.2), 0.3, 0.0, kTwoPi));
    const Profile profile = buildProfile(segments, 1e-9);
    FK_CHECK(profile.regions.size() == 1);
    const ProfileRegion &region = profile.regions[0];
    const Body sheet = planarSheet(kTilted, profile.regions);
    checkSheet(sheet, kTilted.zDir());
    const Body solid = makeExtrusion(kTilted, region, 1.5);
    FaceId base;
    for (FaceId f : solid.faces())
        if (solid.face(f).surface->type() == SurfaceType::Plane && dot(faceNormal(solid, f), kTilted.zDir()) < -0.999 &&
            std::fabs(dot(solid.face(f).surface->point(0, 0) - kTilted.origin(), kTilted.zDir())) < 1e-12)
            base = f;
    FK_CHECK(base.valid());
    const double expected = area(region);
    FK_CHECK_NEAR(totalArea(sheet), expected, 1e-11 * expected);
    FK_CHECK_NEAR(faceArea(solid, base), totalArea(sheet), 1e-11 * expected);
    FK_CHECK(solid.face(base).loops.size() == sheet.face(sheet.faces()[0]).loops.size());
    std::vector<int> types, baseTypes;
    for (EdgeId e : sheet.edges()) types.push_back(int(sheet.edge(e).curve->type()));
    for (LoopId l : solid.face(base).loops)
        for (FinId f : solid.loopFins(l)) baseTypes.push_back(int(solid.edge(solid.fin(f).edge).curve->type()));
    std::sort(types.begin(), types.end());
    std::sort(baseTypes.begin(), baseTypes.end());
    FK_CHECK(types == baseTypes);
    GProp_GProps props;
    BRepGProp::SurfaceProperties(occtFace(region, kTilted), props, 1e-12);
    FK_CHECK_NEAR(totalArea(sheet), props.Mass(), 1e-9 * expected);

    // Le stesse curve date come loop 3D: la stessa lamina.
    std::vector<std::vector<PathSegment>> loops{placed(region.outer.segments)};
    for (const ProfileLoop &hole : region.holes) loops.push_back(placed(hole.segments));
    const Body general = planarSheet(loops);
    checkSheet(general, kTilted.zDir());
    FK_CHECK_NEAR(totalArea(general), expected, 1e-11 * expected);
    // Un contorno orario si gira; due regioni danno due facce.
    ProfileRegion clockwise{reversed(ProfileLoop{rectangle(Vec2(10, 0), 2, 2)}), {}};
    const Body two = planarSheet(kTilted, {region, clockwise});
    checkSheet(two, kTilted.zDir());
    FK_CHECK(two.faces().size() == 2);
    FK_CHECK_NEAR(totalArea(two), expected + 4.0, 1e-11 * expected);
}

// Un foro piccolo vicino al bordo di un cerchio sta tra la corda dei campioni
// e l'arco: l'annidamento con il numero di avvolgimento esatto lo tiene un
// foro (prima diventava una seconda faccia sovrapposta).
FK_TEST(PlanarSheetHoleNearCurvedBoundary) {
    const double a = kPi / 64.0, r = 1e-4;
    for (double distanceFromCenter : {0.9990, 0.9993, 0.9998}) {
        const Vec2 c(distanceFromCenter * std::cos(a), distanceFromCenter * std::sin(a));
        const Body body = planarSheet(std::vector<std::vector<PathSegment>>{placed(std::vector<ProfileSegment>{arcSegment(Vec2(0, 0), 1.0, 0.0, kTwoPi)}), placed(std::vector<ProfileSegment>{arcSegment(c, r, 0.0, kTwoPi)})});
        FK_CHECK(body.faces().size() == 1);
        FK_CHECK_NEAR(totalArea(body), kPi - kPi * r * r, 1e-12);
    }
}

// Contorni che si toccano o si intersecano: errore invece di facce sovrapposte.
FK_TEST(PlanarSheetRejectsTouchingLoops) {
    const std::vector<PathSegment> square = placed(rectangle(Vec2(-2, -2), 4.0, 4.0));
    const auto withHole = [&](const Vec2 &center) {
        return planarSheet(std::vector<std::vector<PathSegment>>{square, placed(std::vector<ProfileSegment>{arcSegment(center, 1.0, 0.0, kTwoPi)})});
    };
    FK_CHECK_THROWS(withHole(Vec2(1, 0)));    // tangente al lato x = 2
    FK_CHECK_THROWS(withHole(Vec2(1.5, 0)));  // lo attraversa
    FK_CHECK(withHole(Vec2(0.5, 0)).faces().size() == 1);
}
