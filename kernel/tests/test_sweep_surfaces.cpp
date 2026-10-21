#include <cmath>

#include "fk_body_check.h"
#include "fk_curve_algo.h"
#include "fk_curve_ops.h"
#include "fk_helix.h"
#include "fk_mass.h"
#include "fk_sweep.h"
#include "fk_tessellate.h"
#include "fk_surface_algo.h"
#include "fk_test_profiles.h"

using namespace fktest;

// Sweep di superfici (sweepSheet): tubi senza coperchi da loop chiusi,
// strisce da catene aperte, anche insieme, sempre lamine.

namespace {

Frame3 normalFrame(const Vec3 &center, const Vec3 &tangent) { return Frame3(center, normalized(tangent), Vec3(0.3, 0.7, 0.2)); }

Vec3 startTangent(const PathSegment &s) { return normalized(s.curve->derivative(s.range.lo)); }

double pathLength(const std::vector<PathSegment> &path) {
    double length = 0.0;
    for (const PathSegment &s : path) length += arcLength(*s.curve, s.range, 1e-13);
    return length;
}

ProfileLoop circleLoop(double r) { return ProfileLoop{{arcSegment(Vec2(0, 0), r, 0.0, kTwoPi)}}; }

ProfileLoop squareLoop(double a) {
    ProfileLoop loop;
    loop.segments = {lineSegment(Vec2(-a / 2, -a / 2), Vec2(a / 2, -a / 2)), lineSegment(Vec2(a / 2, -a / 2), Vec2(a / 2, a / 2)),
                     lineSegment(Vec2(a / 2, a / 2), Vec2(-a / 2, a / 2)), lineSegment(Vec2(-a / 2, a / 2), Vec2(-a / 2, -a / 2))};
    return loop;
}

double totalArea(const Body &body) {
    double area = 0.0;
    for (FaceId f : body.faces()) area += faceArea(body, f);
    return area;
}

int laminarEdges(const Body &body) {
    int count = 0;
    for (EdgeId e : body.edges()) count += body.isLaminar(e);
    return count;
}

// Lamina valida e tassellabile; restituisce la tassellazione.
Tessellation checkedSheet(const Body &body) {
    FK_CHECK(body.isSheet());
    for (const CheckIssue &issue : checkBody(body)) reportFailure(__FILE__, __LINE__, describe(issue.code) + ": " + issue.message);
    TessellationOptions options;
    options.deflection = 0.01;
    const Tessellation mesh = tessellate(body, options);
    FK_CHECK(mesh.failedFaces == 0);
    return mesh;
}

// Le normali della tassellazione escono dal tubo: verso opposto alla
// direzione del punto `axisPoint(p)` piu' vicino sul percorso.
template <typename AxisPoint>
bool outwardNormals(const Tessellation &mesh, AxisPoint axisPoint) {
    for (const FaceMesh &face : mesh.faces)
        for (std::size_t i = 0; i < face.points.size(); ++i)
            if (!(dot(face.normals[i], face.points[i] - axisPoint(face.points[i])) > 0.0)) return false;
    return true;
}

}

FK_TEST(SweepSheetCircleAlongLineAndArc) {
    // Cerchio lungo un segmento: area laterale 2 pi r L, cilindro senza coperchi.
    const double r = 0.5, L = 5.0;
    const Vec3 origin(1, 2, 3), d = normalized(Vec3(0, 1, 1));
    const std::vector<PathSegment> line{{std::make_shared<Line<3>>(origin, d), {0.0, L}}};
    const Body tube = sweepSheet(normalFrame(origin, d), {circleLoop(r)}, line);
    const Tessellation mesh = checkedSheet(tube);
    FK_CHECK(tube.faces().size() == 2);
    for (FaceId f : tube.faces()) FK_CHECK(tube.face(f).surface->type() == SurfaceType::Cylinder);
    FK_CHECK(tube.shells().size() == 1);
    FK_CHECK(laminarEdges(tube) == 4);
    FK_CHECK_NEAR(totalArea(tube), kTwoPi * r * L, 1e-9);
    FK_CHECK(outwardNormals(mesh, [&](const Vec3 &p) { return origin + dot(p - origin, d) * d; }));
    // Il verso del loop non conta: lo stesso cerchio orario da' le stesse normali.
    const Body reversedTube = sweepSheet(normalFrame(origin, d), {reversed(circleLoop(r))}, line);
    FK_CHECK(outwardNormals(checkedSheet(reversedTube), [&](const Vec3 &p) { return origin + dot(p - origin, d) * d; }));
    // Anche con il percorso che esce dal lato opposto della normale del profilo.
    const Body flipped = sweepSheet(normalFrame(origin, -d), {circleLoop(r)}, line);
    FK_CHECK(outwardNormals(checkedSheet(flipped), [&](const Vec3 &p) { return origin + dot(p - origin, d) * d; }));

    // Cerchio lungo un arco: area di Pappus (2 pi r)(R theta), facce toroidali.
    const double R = 3.0, theta = 2.0;
    const auto arc = std::make_shared<Circle<3>>(Vec3(0, 0, 0), Vec3(1, 0, 0), Vec3(0, 1, 0), R);
    const std::vector<PathSegment> bend{{arc, {0.0, theta}}};
    for (SweepOrientation mode : {SweepOrientation::MinimalTwist, SweepOrientation::Frenet}) {
        const Body bent = sweepSheet(normalFrame(arc->point(0.0), startTangent(bend[0])), {circleLoop(r)}, bend, mode);
        const Tessellation m = checkedSheet(bent);
        for (FaceId f : bent.faces()) FK_CHECK(bent.face(f).surface->type() == SurfaceType::Torus);
        FK_CHECK_NEAR(totalArea(bent), kTwoPi * r * R * theta, 1e-9);
        FK_CHECK(outwardNormals(m, [&](const Vec3 &p) {
            const Vec3 radial(p.x(), p.y(), 0.0);
            return R * normalized(radial);
        }));
    }
    // Profilo parallelo al percorso: un loop chiuso non si puo' trascinare.
    FK_CHECK_THROWS(sweepSheet(Frame3(origin, Vec3(1, 0, 0), d), {circleLoop(r)}, line));
}

FK_TEST(SweepSheetSquareAlongSplineMatchesSolid) {
    // Quadrato lungo una spline piana aperta: le facce del tubo sono quelle
    // laterali del solido (area del solido meno i due coperchi a^2).
    const Frame3 plane(Vec3(0, 0, 0), Vec3(0, 0, 1), Vec3(1, 0, 0));
    const auto spline = std::make_shared<BSplineCurve<2>>(3, std::vector<double>{0, 0, 0, 0, 1, 1, 1, 1},
                                                          std::vector<Vec2>{Vec2(0, 0), Vec2(3, 0), Vec2(6, 2), Vec2(9, 2)});
    const std::vector<PathSegment> path{{embedCurve(spline, plane), {0.0, 1.0}}};
    const double a = 0.4;
    const Frame3 start = normalFrame(path[0].curve->point(0.0), startTangent(path[0]));
    const Body tube = sweepSheet(start, {squareLoop(a)}, path);
    checkedSheet(tube);
    FK_CHECK(tube.faces().size() == 4);
    const Body solid = sweepRegions(start, {ProfileRegion{squareLoop(a), {}}}, path);
    const double lateral = massProperties(solid, 1e-11).area - 2.0 * a * a;
    const double area = totalArea(tube);
    FK_CHECK_NEAR(area, lateral, 1e-9 * lateral);
    // Sezioni normali al percorso piano e baricentro sul percorso: area = perimetro L.
    FK_CHECK_NEAR(area, 4.0 * a * pathLength(path), 1e-8 * area);
}

FK_TEST(SweepSheetRoundedRectangleAlongHelix) {
    // Rettangolo arrotondato lungo l'elica (esatta e B-spline): area uguale
    // a quella laterale del solido (area meno i due coperchi in forma chiusa).
    HelixSpec spec;
    spec.frame = Frame3(Vec3(0, 0, 0), Vec3(0, 0, 1), Vec3(1, 0, 0));
    spec.radius = 4.0;
    spec.pitch = 1.5;
    spec.turns = 1.0;
    const auto helix = std::make_shared<HelixCurve>(spec);
    const double w = 0.6, h = 0.4, rc = 0.1;
    const ProfileLoop rounded{roundedRectangle(Vec2(-w / 2, -h / 2), w, h, rc)};
    const double cap = w * h - (4.0 - kPi) * rc * rc;
    const Frame3 start = normalFrame(helix->point(0.0), normalized(helix->derivative(0.0)));
    const auto spline = std::make_shared<BSplineCurve<3>>(helixBSpline(*helix));
    for (const PathSegment &segment : {PathSegment{helix, helix->domain()}, PathSegment{spline, spline->domain()}}) {
        const std::vector<PathSegment> path{segment};
        const Body tube = sweepSheet(start, {rounded}, path, SweepOrientation::Frenet);
        checkedSheet(tube);
        FK_CHECK(tube.faces().size() == 8);
        FK_CHECK(tube.shells().size() == 1);
        const Body solid = sweepRegions(start, {ProfileRegion{rounded, {}}}, path, SweepOrientation::Frenet);
        const double lateral = massProperties(solid, 1e-11).area - 2.0 * cap;
        FK_CHECK_NEAR(totalArea(tube), lateral, 1e-9 * lateral);
    }
}

FK_TEST(SweepSheetMixedProfileAndChains) {
    // Un cerchio e una catena aperta lungo un segmento: due shell, aree esatte.
    const double r = 0.3, L = 4.0;
    const std::vector<PathSegment> line{{std::make_shared<Line<3>>(Vec3(0, 0, 0), Vec3(0, 0, 1)), {0.0, L}}};
    const Frame3 frame(Vec3(0, 0, 0), Vec3(0, 0, 1), Vec3(1, 0, 0));
    ProfileLoop chain;
    chain.segments = {lineSegment(Vec2(0.6, -0.2), Vec2(0.6, 0.2)), arcSegment(Vec2(0.6, 0.4), 0.2, -kHalfPi, kHalfPi)};
    const Body mixed = sweepSheet(frame, {circleLoop(r), chain}, line);
    checkedSheet(mixed);
    FK_CHECK(mixed.shells().size() == 2);
    FK_CHECK_NEAR(totalArea(mixed), (kTwoPi * r + 0.4 + kPi * 0.2) * L, 1e-9);
    // Solo catene: lo stesso risultato di sweepChains.
    const Body chains = sweepSheet(frame, {chain}, line);
    const Body reference = sweepChains(frame, {chain}, line);
    checkedSheet(chains);
    FK_CHECK(chains.faces().size() == reference.faces().size());
    FK_CHECK_NEAR(totalArea(chains), totalArea(reference), 1e-12);
    std::vector<FaceId> a = chains.faces(), b = reference.faces();
    for (std::size_t i = 0; i < a.size() && i < b.size(); ++i) FK_CHECK(chains.face(a[i]).sense == reference.face(b[i]).sense);
    // Catena di lato al percorso (nel suo piano): ammessa come in sweepChains.
    ProfileLoop along;
    along.segments = {lineSegment(Vec2(0, 0), Vec2(1, 0))};
    const Frame3 side(Vec3(0, 0, 0), Vec3(0, 1, 0), Vec3(1, 0, 0));
    const std::vector<PathSegment> shortLine{{std::make_shared<Line<3>>(Vec3(0, 0, 0), Vec3(0, 0, 1)), {0.0, 2.0}}};
    const Body flat = sweepSheet(side, {along}, shortLine);
    checkedSheet(flat);
    FK_CHECK_NEAR(totalArea(flat), 2.0, 1e-12);
    FK_CHECK_THROWS(sweepSheet(frame, {}, line));
}

FK_TEST(SweepSheetClosedPathIsClosedShell) {
    // Cerchio lungo un anello chiuso: shell chiusa senza bordo, ma lamina
    // (region non solida). Area del toro 4 pi^2 R r.
    const double R = 3.0, r = 0.5;
    const auto circle = std::make_shared<Circle<3>>(Vec3(1, 1, 0), Vec3(1, 0, 0), Vec3(0, 1, 0), R);
    const std::vector<PathSegment> ring{{circle, {0.0, kPi}}, {circle, {kPi, kTwoPi}}};
    const Body torus = sweepSheet(normalFrame(circle->point(0.0), startTangent(ring[0])), {circleLoop(r)}, ring);
    const Tessellation mesh = checkedSheet(torus);
    FK_CHECK(torus.shells().size() == 1);
    FK_CHECK(laminarEdges(torus) == 0);
    FK_CHECK_NEAR(totalArea(torus), 4.0 * kPi * kPi * R * r, 1e-9);
    FK_CHECK(outwardNormals(mesh, [&](const Vec3 &p) {
        const Vec3 radial(p.x() - 1.0, p.y() - 1.0, 0.0);
        return Vec3(1, 1, 0) + R * normalized(radial);
    }));
    // Il solido racchiuso resta di sweepRegions.
    const Body solid = sweepRegions(normalFrame(circle->point(0.0), startTangent(ring[0])), {ProfileRegion{circleLoop(r), {}}}, ring);
    FK_CHECK(!solid.isSheet());
    FK_CHECK_NEAR(massProperties(solid, 1e-11).area, totalArea(torus), 1e-9);
    // Quadrato lungo un'ellisse chiusa (superfici B-spline): anche qui niente bordo.
    const auto loop = std::make_shared<Ellipse<3>>(Vec3(0, 0, 0), Vec3(1, 0, 0), Vec3(0, 1, 0), 5.0, 3.0);
    const std::vector<PathSegment> closed{{loop, {0.0, kPi}}, {loop, {kPi, kTwoPi}}};
    const Body band = sweepSheet(normalFrame(loop->point(0.0), startTangent(closed[0])), {squareLoop(0.3)}, closed);
    checkedSheet(band);
    FK_CHECK(laminarEdges(band) == 0);
    const double area = totalArea(band);
    FK_CHECK_NEAR(area, 4.0 * 0.3 * pathLength(closed), 1e-8 * area);
}

// Profilo misto con il percorso verso -n: le catene aperte hanno il verso di
// sweepChains anche accanto ai loop chiusi (che seguono la regola dei solidi).
FK_TEST(SweepSheetMixedChainKeepsChainSense) {
    const Frame3 frame(Vec3(0, 0, 0), Vec3(0, 0, 1), Vec3(1, 0, 0));
    const std::vector<PathSegment> down{{std::make_shared<Line<3>>(Vec3(0, 0, 0), Vec3(0, 0, -1)), {0.0, 3.0}}};
    ProfileLoop chain;
    chain.segments = {lineSegment(Vec2(2, -1), Vec2(2, 1))};
    const Body mixed = sweepSheet(frame, {squareLoop(1.0), chain}, down);
    const Body reference = sweepChains(frame, {chain}, down);
    checkedSheet(mixed);
    const auto stripNormal = [](const Body &body) {
        for (FaceId f : body.faces()) {
            const Surface &s = *body.face(f).surface;
            if (s.type() != SurfaceType::Plane) continue;
            const SurfaceProjection foot = projectPoint(s, Vec3(2, 0, -1.5));
            if (foot.distance > 1e-9) continue;
            const Vec3 n = s.normal(foot.u, foot.v);
            return body.face(f).sense ? n : -n;
        }
        return Vec3();
    };
    FK_CHECK_NEAR(dot(stripNormal(mixed), stripNormal(reference)), 1.0, 1e-12);
}
