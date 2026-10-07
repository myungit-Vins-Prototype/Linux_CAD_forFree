#include <cstdio>
#include <fstream>
#include <sstream>
#include <cmath>

#include "fk_blend.h"
#include "fk_classify.h"
#include "fk_body_check.h"
#include "fk_body_io.h"
#include "fk_bspline_surface.h"
#include "fk_curve_algo.h"
#include "fk_extrude.h"
#include "fk_helix.h"
#include "fk_step.h"
#include "fk_sweep.h"
#include "fk_loft.h"
#include "fk_mass.h"
#include "fk_offset.h"
#include "fk_primitives.h"
#include "fk_profile.h"
#include "fk_revolve.h"
#include "fk_sew.h"
#include "fk_sheet.h"
#include "fk_surface_algo.h"
#include "fk_tessellate.h"
#include "fk_test_profiles.h"

using namespace fktest;

namespace {

double totalArea(const Body &body) {
    double area = 0.0;
    for (FaceId f : body.faces()) area += faceArea(body, f);
    return area;
}

void checkValid(const Body &body) {
    for (const CheckIssue &issue : checkBody(body)) reportFailure(__FILE__, __LINE__, describe(issue.code) + ": " + issue.message);
    TessellationOptions options;
    options.deflection = 0.01;
    FK_CHECK(tessellate(body, options).failedFaces == 0);
}

// Distanza massima dei punti della tassellazione di `offset` dalla distanza
// `expected` da `reference` (lungo la normale: proiezione sulla superficie).
double distanceError(const Body &offset, const Body &reference, const std::vector<FaceId> &faces, double expected) {
    TessellationOptions options;
    options.deflection = 0.02;
    const Tessellation mesh = tessellate(offset, options);
    double worst = 0.0;
    for (const FaceMesh &face : mesh.faces)
        for (const Vec3 &p : face.points) {
            double best = 1e300;
            // Solo i piedi che stanno nelle facce (le superfici sono illimitate).
            for (FaceId f : faces) {
                const SurfaceProjection foot = projectPoint(*reference.face(f).surface, p);
                if (classifyPointOnFace(reference, f, foot.point, 1e-6) != PointLocation::Outside) best = std::min(best, foot.distance);
            }
            if (best > 1e299) continue;  // piede su un bordo tra due facce: vale la distanza dallo spigolo
            worst = std::max(worst, std::fabs(best - std::fabs(expected)));
        }
    return worst;
}

std::vector<FaceId> facesOfType(const Body &body, SurfaceType type) {
    std::vector<FaceId> result;
    for (FaceId f : body.faces())
        if (body.face(f).surface->type() == type) result.push_back(f);
    return result;
}

std::vector<FaceId> sideFaces(const Body &body) {
    std::vector<FaceId> result;
    for (FaceId f : body.faces())
        if (body.face(f).surface->type() != SurfaceType::Plane || std::fabs(body.face(f).surface->normal(0, 0).z()) < 0.5) result.push_back(f);
    return result;
}

}

// Superfici analitiche: dello stesso tipo, esatte, con la stessa parametrizzazione.
FK_TEST(OffsetAnalyticSurfaces) {
    const Frame3 frame(Vec3(1, -2, 3), Vec3(0.2, 0.3, 1), Vec3(1, 0, 0));
    const std::vector<std::shared_ptr<Surface>> surfaces{
        std::make_shared<Plane>(frame), std::make_shared<CylindricalSurface>(frame, 2.0), std::make_shared<ConicalSurface>(frame, 0.4, 1.5),
        std::make_shared<SphericalSurface>(frame, 3.0), std::make_shared<ToroidalSurface>(frame, 5.0, 1.2)};
    for (const auto &surface : surfaces)
        for (double d : {0.3, -0.25}) {
            const SurfacePtr offset = offsetSurface(*surface, d, Interval{0.1, 2.0}, Interval{-0.8, 0.9});
            FK_CHECK(offset->type() == surface->type());
            for (double u : {0.2, 1.1, 1.9})
                for (double v : {-0.7, 0.1, 0.8}) {
                    const Vec3 expected = surface->point(u, v) + d * surface->normal(u, v);
                    FK_CHECK_NEAR(distance(offset->point(u, v), expected), 0.0, 1e-12);
                    FK_CHECK_NEAR(dot(offset->normal(u, v), surface->normal(u, v)), 1.0, 1e-12);
                }
        }
    FK_CHECK_THROWS(offsetSurface(CylindricalSurface(frame, 2.0), -2.5, Interval{0, 1}, Interval{0, 1}));
}

// Estrusioni e rivoluzioni di spline: curva base e meridiano a distanza
// (Hermite) nello stesso parametro; B-spline: bicubica entro la tolleranza.
FK_TEST(OffsetFreeformSurfaces) {
    auto open = std::make_shared<BSplineCurve<2>>(3, std::vector<double>{0, 0, 0, 0, 0.5, 1, 1, 1, 1},
                                                  std::vector<Vec2>{Vec2(0, 0), Vec2(1, 2), Vec2(3, -1), Vec2(4, 1), Vec2(6, 0)},
                                                  std::vector<double>{1, 1.5, 0.8, 1.2, 1});
    const ProfileSegment spline{open, open->domain()};
    const CurvePtr<3> base = embedCurve(spline.curve, Frame3());
    const ExtrusionSurface extrusion(base, Vec3(0, 0, 1));
    auto meridian = std::make_shared<BSplineCurve<3>>(3, std::vector<double>{0, 0, 0, 0, 1, 1, 1, 1},
                                                      std::vector<Vec3>{Vec3(2, 0, 0), Vec3(3, 0, 1), Vec3(1.5, 0, 2), Vec3(2.5, 0, 3)});
    const RevolutionSurface revolution(meridian, Vec3(), Vec3(0, 0, 1));
    std::vector<Vec3> poles;
    for (int i = 0; i < 5; ++i)
        for (int j = 0; j < 4; ++j) poles.push_back(Vec3(i, j, 0.3 * std::sin(1.3 * i + 0.7 * j)));
    const BSplineSurface patch(3, 3, {0, 0, 0, 0, 0.5, 1, 1, 1, 1}, {0, 0, 0, 0, 1, 1, 1, 1}, 5, 4, poles);
    const struct {
        const Surface *surface;
        Interval u, v;
        SurfaceType type;
    } cases[] = {{&extrusion, spline.range, {0.0, 2.0}, SurfaceType::Extrusion},
                 {&revolution, {0.0, kTwoPi}, {0.0, 1.0}, SurfaceType::Revolution},
                 {&patch, {0.0, 1.0}, {0.0, 1.0}, SurfaceType::BSpline}};
    for (const auto &c : cases)
        for (double d : {0.2, -0.15}) {
            const SurfacePtr offset = offsetSurface(*c.surface, d, c.u, c.v, 1e-8);
            FK_CHECK(offset->type() == c.type);
            double worst = 0.0;
            for (int i = 0; i <= 10; ++i)
                for (int j = 0; j <= 10; ++j) {
                    const double u = c.u.lo + c.u.length() * i / 10.0, v = c.v.lo + c.v.length() * j / 10.0;
                    worst = std::max(worst, distance(offset->point(u, v), c.surface->point(u, v) + d * c.surface->normal(u, v)));
                }
            FK_CHECK(worst < 1e-7);
        }
}

// Facce tangenti tra loro: una sola superficie cucita. Il fianco di un
// rettangolo arrotondato (piani e cilindri) a distanza d ha il perimetro
// P + 2 pi d; le facce di un parallelepipedo (spigoli vivi) restano sei.
FK_TEST(OffsetFacesJoinsTangentFaces) {
    const double width = 8.0, height = 5.0, radius = 1.5, depth = 3.0, d = 0.4;
    const ProfileRegion region = buildProfile(roundedRectangle(Vec2(0, 0), width, height, radius), 1e-9).regions.front();
    const Body prism = makeExtrusion(Frame3(), region, depth);
    const std::vector<FaceId> sides = sideFaces(prism);
    FK_CHECK(sides.size() == 8);
    const OffsetResult outward = offsetFaces(prism, sides, d);
    checkValid(outward.body);
    FK_CHECK(outward.body.isSheet());
    FK_CHECK(outward.shells == 1);
    FK_CHECK(outward.sharpEdges == 0);
    const double perimeter = 2.0 * (width + height - 4.0 * radius) + kTwoPi * radius;
    FK_CHECK_NEAR(totalArea(outward.body), (perimeter + kTwoPi * d) * depth, 1e-9);
    const OffsetResult inward = offsetFaces(prism, sides, -d);
    checkValid(inward.body);
    FK_CHECK_NEAR(totalArea(inward.body), (perimeter - kTwoPi * d) * depth, 1e-9);

    const Body box = makeBox(Frame3(), 2.0, 3.0, 4.0);
    const OffsetResult faces = offsetFaces(box, box.faces(), 0.5);
    checkValid(faces.body);
    FK_CHECK(faces.shells == 6);
    FK_CHECK(faces.sharpEdges == 12);
    FK_CHECK_NEAR(totalArea(faces.body), 2.0 * (6.0 + 8.0 + 12.0), 1e-10);
}

// Sfera intera (faccia senza bordo) e fianco di una spline estrusa:
// lunghezza della curva a distanza L + 2 pi d (curva chiusa senza cuspidi).
FK_TEST(OffsetFacesCurvedAndFreeform) {
    const Body sphere = makeSphere(Frame3(Vec3(1, 2, 3), Vec3(0, 0, 1), Vec3(1, 0, 0)), 2.0);
    const OffsetResult grown = offsetFaces(sphere, sphere.faces(), 0.5);
    checkValid(grown.body);
    FK_CHECK_NEAR(totalArea(grown.body), 4.0 * kPi * 2.5 * 2.5, 1e-9);

    // Ellisse estrusa (ExtrusionSurface): la curva base a distanza e' una cubica di Hermite.
    const ProfileSegment ellipse{std::make_shared<Ellipse<2>>(Vec2(0, 0), Vec2(1, 0), Vec2(0, 1), 3.0, 1.8), Interval{0.0, kTwoPi}};
    const Body prism = makeExtrusion(Frame3(), buildProfile({ellipse}, 1e-9).regions.front(), 2.0);
    const std::vector<FaceId> sides = facesOfType(prism, SurfaceType::Extrusion);
    FK_CHECK(sides.size() == 1);
    const double length = arcLength(*ellipse.curve, ellipse.range), d = 0.1;
    const OffsetResult side = offsetFaces(prism, sides, d, 1e-8);
    checkValid(side.body);
    FK_CHECK_NEAR(totalArea(side.body), (length + kTwoPi * d) * 2.0, 1e-6);
    FK_CHECK(distanceError(side.body, prism, sides, d) < 1e-6);
    // Spline chiusa con un angolo nella chiusura: la faccia ha uno spigolo
    // vivo al suo interno e la superficie a distanza non si richiude.
    const Body cornered = makeExtrusion(Frame3(), buildProfile({closedSpline(Vec2(0, 0), 3.0, false)}, 1e-9).regions.front(), 2.0);
    FK_CHECK_THROWS(offsetFaces(cornered, facesOfType(cornered, SurfaceType::Extrusion), d));

    // Raccordo su un parallelepipedo: faccia sopra, raccordo e fianco tangenti in una superficie.
    const Body box = makeBox(Frame3(), 4.0, 3.0, 2.0);
    const Body rounded = blendEdges(box, std::vector<EdgeId>{nearestEdge(box, Vec3(2.0, 0.0, 2.0), 1e-6)}, 0.6, false);
    std::vector<FaceId> chain;
    for (FaceId f : rounded.faces()) {
        const Surface &s = *rounded.face(f).surface;
        if (s.type() == SurfaceType::Cylinder) chain.push_back(f);
        else if (s.type() == SurfaceType::Plane) {
            const Vec3 n = rounded.face(f).sense ? s.normal(0, 0) : -s.normal(0, 0);
            if (n.z() > 0.9 || n.y() < -0.9) chain.push_back(f);
        }
    }
    FK_CHECK(chain.size() == 3);
    const OffsetResult skin = offsetFaces(rounded, chain, 0.25);
    checkValid(skin.body);
    FK_CHECK(skin.shells == 1);
    const double skinError = distanceError(skin.body, rounded, chain, 0.25);
    if (!(skinError < 1e-7)) std::printf("errore raccordo %g\n", skinError);
    FK_CHECK(skinError < 1e-7);
}

// Facce B-spline di un loft liscio: distanza costante entro la tolleranza.
FK_TEST(OffsetFacesLoft) {
    std::vector<LoftSection> sections;
    for (int k = 0; k < 3; ++k) {
        LoftSection s;
        s.frame = Frame3(Vec3(0.3 * k, 0, 2.0 * k), Vec3(0, 0, 1), Vec3(1, 0, 0));
        s.loop.segments = {arcSegment(Vec2(0, 0), 2.0 + 0.6 * std::sin(double(k)), 0.0, kTwoPi)};
        sections.push_back(s);
    }
    const Body loft = loftSolid(sections, false);
    const std::vector<FaceId> sides = facesOfType(loft, SurfaceType::BSpline);
    FK_CHECK(!sides.empty());
    const OffsetResult skin = offsetFaces(loft, sides, 0.3, 1e-8);
    checkValid(skin.body);
    FK_CHECK(skin.shells == 1);
    FK_CHECK(distanceError(skin.body, loft, sides, 0.3) < 1e-6);
}

// Cucitura: due meta' di un parallelepipedo tornano un solido; con la faccia
// sopra in due rettangoli il bordo della scatola si divide nel vertice comune.
FK_TEST(SewSheetsIntoSolid) {
    const Body box = makeBox(Frame3(), 2.0, 3.0, 4.0);
    std::vector<FaceId> first, second;
    for (FaceId f : box.faces()) (f.index % 2 ? first : second).push_back(f);
    const Body a = facesAsSheet(box, first), b = facesAsSheet(box, second);
    FK_CHECK(a.isSheet() && b.isSheet());
    const SewResult sewn = sewSheets({&a, &b}, 1e-6, true);
    FK_CHECK(sewn.closed && sewn.solid && sewn.freeEdges == 0);
    checkValid(sewn.body);
    FK_CHECK_NEAR(massProperties(sewn.body).volume, 24.0, 1e-10);
    const SewResult open = sewSheets({&a}, 1e-6, true);
    FK_CHECK(!open.closed && !open.solid && open.freeEdges > 0 && open.body.isSheet());

    // Scatola senza coperchio e coperchio in due rettangoli (giunzione a T).
    std::vector<FaceId> walls;
    for (FaceId f : box.faces()) {
        const Surface &s = *box.face(f).surface;
        const Vec3 n = box.face(f).sense ? s.normal(0, 0) : -s.normal(0, 0);
        if (n.z() < 0.9) walls.push_back(f);
    }
    const Body cup = facesAsSheet(box, walls);
    const auto lid = [](double x0, double x1) {
        const Profile profile = buildProfile({lineSegment(Vec2(x0, 0), Vec2(x1, 0))}, 1e-9);
        return makeSheetExtrusion(Frame3(Vec3(0, 0, 4), Vec3(0, -1, 0), Vec3(1, 0, 0)), profile.chains, -3.0);
    };
    const Body left = lid(0.0, 1.2), right = lid(1.2, 2.0);
    const SewResult closed = sewSheets({&cup, &left, &right}, 1e-6, true);
    FK_CHECK(closed.closed && closed.solid);
    checkValid(closed.body);
    FK_CHECK_NEAR(massProperties(closed.body).volume, 24.0, 1e-10);
    // Le facce girate verso l'interno si raddrizzano.
    const SewResult reversed = sewSheets({&b, &a}, 1e-6, true);
    FK_CHECK_NEAR(massProperties(reversed.body).volume, 24.0, 1e-10);

}

// Offset di tutte le facce tangenti di un solido liscio e cucitura in un solido.
FK_TEST(OffsetThenSewSolid) {
    const Body sphere = makeSphere(Frame3(), 1.5);
    const OffsetResult shell = offsetFaces(sphere, sphere.faces(), 0.5);
    const SewResult solid = sewSheets({&shell.body}, 1e-6, true);
    FK_CHECK(solid.solid);
    FK_CHECK_NEAR(massProperties(solid.body).volume, 4.0 / 3.0 * kPi * 8.0, 1e-9);
}

// Le facce a distanza stanno a |d| dalle facce di partenza: punti dei bordi
// delle facce del risultato (sulle curve esatte) proiettati sul corpo di partenza.
static void checkOffsetDistance(const Body &original, const Body &offset, double d) {
    double worst = 0.0;
    for (EdgeId e : offset.edges()) {
        const Edge &edge = offset.edge(e);
        for (int k = 1; k < 4; ++k) {
            const Vec3 p = edge.curve->point(edge.range.lo + edge.range.length() * k / 4.0);
            double nearest = 1e300;
            for (FaceId f : original.faces()) {
                const SurfaceProjection at = projectPoint(*original.face(f).surface, p);
                if (std::fabs(at.distance - std::fabs(d)) < std::fabs(nearest - std::fabs(d)))
                    if (classifyPointOnFace(original, f, original.face(f).surface->point(at.u, at.v), 1e-4) != PointLocation::Outside)
                        nearest = at.distance;
            }
            worst = std::max(worst, std::fabs(nearest - std::fabs(d)));
        }
    }
    FK_CHECK(worst < 1e-5);
}

// Molla (sweep di un cerchio lungo un'elica: B-spline con centinaia di
// campate lungo il percorso). Prima: superficie "non approssimabile" (la
// griglia dei nodi raddoppiata superava il limite) e, con le SP-curve
// ricalcolate per proiezione, decine di secondi.
FK_TEST(OffsetFacesSpring) {
    HelixSpec spec;
    spec.frame = Frame3(Vec3(0, 0, 0), Vec3(0, 0, 1), Vec3(1, 0, 0));
    spec.radius = 3.0;
    spec.pitch = 2.0;
    spec.turns = 2.0;
    auto helix = std::make_shared<HelixCurve>(spec);
    Vec3 d[2];
    helix->evaluate(helix->domain().lo, 1, d);
    const Vec3 t = normalized(d[1]);
    const Frame3 profileFrame(d[0], t, Vec3(1, 0, 0) - dot(Vec3(1, 0, 0), t) * t);
    const auto circle = std::make_shared<Circle<2>>(makeCircle(Vec2(0, 0), 0.5));
    const Body spring = sweepRegions(profileFrame, buildProfile({{circle, {0.0, kTwoPi}}}, 1e-9).regions, {{helix, helix->domain()}});
    for (double distance : {0.1, -0.1}) {
        try {
            const OffsetResult result = offsetFaces(spring, spring.faces(), distance);
            FK_CHECK(result.shells == 3);  // tubo e due tappi (spigoli vivi tra loro)
            checkOffsetDistance(spring, result.body, distance);
        } catch (const std::exception &error) {
            fktest::reportFailure(__FILE__, __LINE__, error.what());
        }
    }
}

// Corpi importati con centinaia di facce B-spline e superfici di rivoluzione
// (un toro scritto come rivoluzione di un cerchio, con la faccia a cavallo
// della cucitura del meridiano): tutte le facce a distanza. Prima tutti e tre
// fallivano (finestra del meridiano su tutto il periodo, B-spline fuori dal
// dominio, curve a distanza "con cuspidi" dove la faccia non arriva).
FK_TEST(OffsetFacesImportedParts) {
    for (const char *name : {"AP0730-REV00.STEP", "L407-P3.STEP", "flacone.step"}) {
        std::ifstream in(std::string(FORGECAD_SOURCE_DIR) + "/File_Esempio/" + name, std::ios::binary);
        if (!in) continue;  // file d'esempio non presente
        std::stringstream content;
        content << in.rdbuf();
        const StepReadResult read = readStep(content.str());
        FK_CHECK(!read.bodies.empty());
        if (read.bodies.empty()) continue;
        const Body &body = read.bodies.front().body;
        try {
            const OffsetResult result = offsetFaces(body, body.faces(), 0.02);
            FK_CHECK(result.body.faces().size() == body.faces().size());
            FK_CHECK(checkBody(result.body).empty());
        } catch (const std::exception &error) {
            fktest::reportFailure(__FILE__, __LINE__, std::string(name) + ": " + error.what());
        }
    }
}

FK_TEST(OffsetPreservesSharpSeams) {
    const Body box = makeBox(Frame3(), 2.0, 3.0, 4.0);
    for (double d : {0.25, -0.25}) {
        const auto result = offsetFaces(box, box.faces(), d, 1e-7, true);
        checkValid(result.body);
        FK_CHECK(result.shells == 1 && result.sharpEdges == 0);
        for (EdgeId edge : result.body.edges()) FK_CHECK(!result.body.isLaminar(edge));
        const auto solid = sewSheets({&result.body}, 1e-6, true);
        FK_CHECK(solid.solid);
        FK_CHECK_NEAR(massProperties(solid.body).volume, (2 + 2*d) * (3 + 2*d) * (4 + 2*d), 1e-6);
    }
    const auto separate = offsetFaces(box, box.faces(), 0.25, 1e-7, false);
    FK_CHECK(separate.shells == 6);
    const auto partial = offsetFaces(box, {box.faces()[0], box.faces()[2]}, 0.25, 1e-7, true);
    checkValid(partial.body);
    FK_CHECK(partial.shells == 1);
    const Body cylinder = makeCylinder(Frame3(), 2.0, 4.0);
    const auto result = offsetFaces(cylinder, cylinder.faces(), 0.25, 1e-7, true);
    checkValid(result.body);
    FK_CHECK(result.shells == 1);
    const auto solid = sewSheets({&result.body}, 1e-6, true);
    FK_CHECK(solid.solid);
    FK_CHECK_NEAR(massProperties(solid.body).volume, kPi * 2.25 * 2.25 * 4.5, 1e-5);
}

FK_TEST(OffsetLoftExtension) {
    std::vector<LoftSection> sections;
    for (int k = 0; k < 3; ++k) {
        LoftSection s;
        s.frame = Frame3(Vec3(0.2 * k, 0, 3.0 * k), Vec3(0, 0, 1), Vec3(1, 0, 0));
        s.loop.segments = {arcSegment(Vec2(), 2.0 + 0.2 * k, 0.0, kPi)};
        sections.push_back(s);
    }
    const Body loft = loftSheet(sections, false);
    const auto offset = offsetFaces(loft, loft.faces(), 0.2);
    checkValid(offset.body);
    int tested = 0;
    for (EdgeId edge : offset.body.edges()) {
        if (!offset.body.isLaminar(edge)) continue;
        const Body extended = extendSheet(offset.body, {edge}, 0.3);
        checkValid(extended);
        FK_CHECK(totalArea(extended) > totalArea(offset.body));
        ++tested;
    }
    FK_CHECK(tested >= 4);
}

FK_TEST(OffsetLoftMixedSections) {
    LoftSection square, circle;
    square.frame = Frame3();
    const std::vector<Vec2> points{Vec2(2,2),Vec2(-2,2),Vec2(-2,-2),Vec2(2,-2)};
    for (int i=0;i<4;++i) square.loop.segments.push_back(lineSegment(points[i],points[(i+1)%4]));
    circle.frame = Frame3(Vec3(0,0,5), Vec3(0,0,1), Vec3(1,0,0));
    circle.loop.segments = {arcSegment(Vec2(), 2.0, 0.0, kTwoPi)};
    for (bool ruled : {false,true}) {
        const Body loft = loftSheet({square,circle},ruled);
        for (FaceId face : loft.faces()) {
            const auto offset = offsetFaces(loft,{face},0.1);
            checkValid(offset.body);
        }
    }
}

FK_TEST(OffsetLoftSewnCaps) {
    std::vector<LoftSection> sections;
    for (int k = 0; k < 3; ++k) {
        LoftSection s;
        s.frame = Frame3(Vec3(0.1 * k, 0, 3.0 * k), Vec3(0, 0, 1), Vec3(1, 0, 0));
        s.loop.segments = {arcSegment(Vec2(), 2.0 + 0.2 * k, 0.0, kTwoPi)};
        sections.push_back(s);
    }
    const Body loft = loftSolid(sections, false);
    for (double d : {0.1, -0.1}) {
        const auto offset = offsetFaces(loft, loft.faces(), d, 1e-7, true);
        checkValid(offset.body);
        FK_CHECK(offset.shells == 1);
        for (EdgeId edge : offset.body.edges()) FK_CHECK(!offset.body.isLaminar(edge));
    }
}

// Una zona molto curva non e' una cuspide: la suddivisione deve scendere
// sotto il passo usato per stimare le derivate, senza fermarsi prematuramente.
FK_TEST(OffsetFitCurveSmallSmoothFeature) {
    const auto point = [](double t) { return Vec3(t, std::sqrt((t - 0.5) * (t - 0.5) + 1e-12), 0); };
    const auto curve = fitCurve(point, {0, 1}, {}, 1e-10);
    double worst = 0;
    for (int k = -100; k <= 100; ++k) {
        const double t = 0.5 + 1e-7 * k;
        worst = std::max(worst, distance(curve->point(t), point(t)));
    }
    FK_CHECK(worst < 1e-8);
}

FK_TEST(OffsetReportsInternalNormalDiscontinuity) {
    // Due pezze piane cucite solo nei poli, ma dentro un'unica faccia spline.
    // E' la stessa causa del loft del file Loft_offset.prt: non una distanza
    // troppo grande e non un problema risolvibile infittendo la griglia.
    const BSplineSurface creased(1, 1, {0,0,0.5,1,1}, {0,0,1,1}, 3, 2,
        {Vec3(0,0,0),Vec3(0,1,0),Vec3(1,0,0),Vec3(1,1,0),Vec3(2,0,0.2),Vec3(2,1,0.2)});
    for (double d : {1.0, -1.0, 0.1}) {
        bool diagnosed = false;
        try { offsetSurface(creased, d, {0,1}, {0,1}); }
        catch (const std::domain_error &e) { diagnosed = std::string(e.what()).find("discontinuita della normale") != std::string::npos; }
        FK_CHECK(diagnosed);
    }
    // Una selezione limitata a una sola pezza resta perfettamente valida.
    const auto offset = offsetSurface(creased, 1.0, {0,0.4}, {0,1});
    FK_CHECK_NEAR(distance(offset->point(0.2,0.5), Vec3(0.4,0.5,1)), 0.0, 1e-9);
}

// Geometria di Loft_offset.prt: due laterali quadratiche razionali, con
// campate strette e salti di normale interni. Il comando deve produrre
// geometria valida, non soltanto diagnosticare il salto.
FK_TEST(OffsetLoftInternalCreasesOneMillimeter) {
    std::ifstream in(std::string(FORGECAD_SOURCE_DIR) + "/kernel/tests/data/offset_loft.body", std::ios::binary);
    FK_CHECK(bool(in));
    if (!in) return;
    std::stringstream data;
    data << in.rdbuf();
    const Body source = readBodyBinary(data.str());
    const auto sides = facesOfType(source, SurfaceType::BSpline);
    FK_CHECK(sides.size() == 2);
    for (bool sew : {false, true})
        for (double d : {1.0, -1.0}) {
            const auto result = offsetFaces(source, sides, d, 1e-7, sew);
            checkValid(result.body);
            FK_CHECK(result.body.faces().size() == 6);
            FK_CHECK(sew ? result.shells == 1 : result.shells > 1);
            FK_CHECK(!result.notes.empty());
            std::size_t index = 0;
            for (FaceId original : sides) {
                const Face &face = source.face(original);
                const auto &spline = static_cast<const BSplineSurface &>(*face.surface);
                for (const auto &patch : *spline.cachedBezierPatches()) {
                    const Surface &offset = *result.body.face(result.body.faces()[index++]).surface;
                    for (double fu : {0.25, 0.5, 0.75})
                        for (double fv : {0.125, 0.375, 0.625, 0.875}) {
                            const double u = patch.uDomain().lo + fu * patch.uDomain().length();
                            const double v = patch.vDomain().lo + fv * patch.vDomain().length();
                            const Vec3 expected = patch.point(u,v) + (face.sense ? d : -d) * patch.normal(u,v);
                            FK_CHECK_NEAR(distance(offset.point(u,v), expected), 0.0, 2e-6);
                        }
                }
            }
            // La cucitura non deve mascherare il salto gonfiando le tolleranze.
            for (EdgeId e : result.body.edges()) FK_CHECK(result.body.edge(e).tolerance < 5e-6);
            if (sew && d > 0.0)
                for (EdgeId e : result.body.edges())
                    if (result.body.isLaminar(e)) checkValid(extendSheet(result.body, {e}, 0.2));
        }
}
