#include <BRepGProp.hxx>
#include <BRepOffsetAPI_MakePipe.hxx>
#include <GProp_GProps.hxx>

#include <cmath>

#include "fk_body_check.h"
#include "fk_curve_algo.h"
#include "fk_curve_ops.h"
#include "fk_helix.h"
#include "fk_mass.h"
#include "fk_sweep.h"
#include "fk_tessellate.h"
#include "fk_test_profiles.h"

using namespace fktest;

namespace {

// Profilo circolare (raggio r, eventualmente con foro concentrico) nel piano per `center` normale a `tangent`.
Frame3 normalFrame(const Vec3 &center, const Vec3 &tangent) { return Frame3(center, normalized(tangent), Vec3(0.3, 0.7, 0.2)); }

ProfileRegion circleRegion(double r, double hole = 0.0) {
    ProfileRegion region;
    region.outer.segments = {arcSegment(Vec2(0, 0), r, 0.0, kTwoPi)};
    if (hole > 0.0) region.holes.push_back(reversed(ProfileLoop{{arcSegment(Vec2(0, 0), hole, 0.0, kTwoPi)}}));
    return region;
}

ProfileRegion squareRegion(double a) {
    ProfileRegion region;
    region.outer.segments = {lineSegment(Vec2(-a / 2, -a / 2), Vec2(a / 2, -a / 2)), lineSegment(Vec2(a / 2, -a / 2), Vec2(a / 2, a / 2)),
                             lineSegment(Vec2(a / 2, a / 2), Vec2(-a / 2, a / 2)), lineSegment(Vec2(-a / 2, a / 2), Vec2(-a / 2, -a / 2))};
    return region;
}

MassProperties checkedSolid(const Body &body) {
    for (const CheckIssue &issue : checkBody(body)) reportFailure(__FILE__, __LINE__, describe(issue.code) + ": " + issue.message);
    FK_CHECK(!body.isSheet());
    TessellationOptions options;
    options.deflection = 0.01;
    FK_CHECK(tessellate(body, options).failedFaces == 0);
    return massProperties(body, 1e-11);
}

Vec3 startTangent(const PathSegment &s) { return normalized(s.curve->derivative(s.range.lo)); }

double pathLength(const std::vector<PathSegment> &path) {
    double length = 0.0;
    for (const PathSegment &s : path) length += arcLength(*s.curve, s.range, 1e-13);
    return length;
}

// Percorso piano: segmento, arco tangente, spline tangente all'arco.
std::vector<PathSegment> planarPath() {
    const Frame3 plane(Vec3(0, 0, 0), Vec3(0, 0, 1), Vec3(1, 0, 0));
    const auto line = std::make_shared<Line<2>>(Vec2(0, 0), Vec2(1, 0));
    const auto arc = std::make_shared<Circle<2>>(makeCircle(Vec2(4, 3), 3.0));
    // Spline dalla fine dell'arco (7, 3), tangente +Y.
    const auto spline = std::make_shared<BSplineCurve<2>>(3, std::vector<double>{0, 0, 0, 0, 0.5, 1, 1, 1, 1},
                                                          std::vector<Vec2>{Vec2(7, 3), Vec2(7, 5), Vec2(5, 8), Vec2(9, 9), Vec2(12, 7)});
    return {{embedCurve(line, plane), {0.0, 4.0}}, {embedCurve(arc, plane), {-kHalfPi, 0.0}}, {embedCurve(spline, plane), {0.0, 1.0}}};
}

}

FK_TEST(SweepLineAndArcAreExact) {
    // Cerchio lungo un segmento: cilindro; lungo un arco: toro (Pappus).
    const double r = 0.5, R = 3.0;
    const std::vector<PathSegment> line{{std::make_shared<Line<3>>(Vec3(1, 2, 3), normalized(Vec3(0, 1, 1))), {0.0, 5.0}}};
    const Body cylinder = sweepRegions(normalFrame(Vec3(1, 2, 3), normalized(Vec3(0, 1, 1))), {circleRegion(r)}, line);
    FK_CHECK_NEAR(checkedSolid(cylinder).volume, kPi * r * r * 5.0, 1e-10);
    for (FaceId f : cylinder.faces()) {
        const SurfaceType type = cylinder.face(f).surface->type();
        FK_CHECK(type == SurfaceType::Cylinder || type == SurfaceType::Plane);
    }
    const auto arc = std::make_shared<Circle<3>>(Vec3(0, 0, 0), Vec3(1, 0, 0), Vec3(0, 1, 0), R);
    const std::vector<PathSegment> bend{{arc, {0.0, 2.0}}};
    for (SweepOrientation mode : {SweepOrientation::MinimalTwist, SweepOrientation::Frenet}) {
        const Body torus = sweepRegions(normalFrame(arc->point(0.0), startTangent(bend[0])), {circleRegion(r, 0.2)}, bend, mode);
        const MassProperties m = checkedSolid(torus);
        FK_CHECK_NEAR(m.volume, kPi * (r * r - 0.04) * R * 2.0, 1e-10);
        int tori = 0;
        for (FaceId f : torus.faces()) tori += torus.face(f).surface->type() == SurfaceType::Torus;
        FK_CHECK(tori == 4);
    }
    // Quadrato lungo l'arco con il profilo spostato dal percorso (Pappus con il baricentro a R + 0.3).
    ProfileRegion offset = squareRegion(0.4);
    for (ProfileSegment &s : offset.outer.segments) s = {std::make_shared<Line<2>>(s.start() + Vec2(0.3, 0), s.end() - s.start()), {0.0, 0.4}};
    const Frame3 radial(arc->point(0.0), Vec3(0, 1, 0), Vec3(1, 0, 0));
    FK_CHECK_NEAR(checkedSolid(sweepRegions(radial, {offset}, bend)).volume, 0.16 * (R + 0.3) * 2.0, 1e-10);
}

FK_TEST(SweepPlanarPathIsTube) {
    // Tubo lungo segmento + arco + spline: V = A L, area laterale = perimetro L (baricentro sul percorso, sezioni normali).
    const std::vector<PathSegment> path = planarPath();
    const double L = pathLength(path), r = 0.4, h = 0.25;
    const Body tube = sweepRegions(normalFrame(path[0].curve->point(0.0), startTangent(path[0])), {circleRegion(r, h)}, path);
    const MassProperties m = checkedSolid(tube);
    FK_CHECK_NEAR(m.volume, kPi * (r * r - h * h) * L, 1e-8 * m.volume);
    FK_CHECK_NEAR(m.area, kTwoPi * (r + h) * L + 2.0 * kPi * (r * r - h * h), 1e-8 * m.area);
    // Anche il profilo quadrato (le facce sul tratto della spline sono B-spline, quelle sul segmento piani).
    const MassProperties square = checkedSolid(sweepRegions(normalFrame(path[0].curve->point(0.0), startTangent(path[0])), {squareRegion(0.5)}, path));
    FK_CHECK_NEAR(square.volume, 0.25 * L, 1e-8 * 0.25 * L);
    // Un angolo vivo nel percorso non si accetta.
    const std::vector<PathSegment> corner{path[0], {std::make_shared<Line<3>>(Vec3(4, 0, 0), Vec3(0, 1, 0)), {0.0, 2.0}}};
    FK_CHECK_THROWS(sweepRegions(normalFrame(Vec3(0, 0, 0), Vec3(1, 0, 0)), {circleRegion(r)}, corner));
    // Raggio di curvatura minore del profilo: le sezioni si incrocerebbero.
    FK_CHECK_THROWS(sweepRegions(normalFrame(path[1].curve->point(-kHalfPi), startTangent(path[1])), {circleRegion(3.5)}, {path[1]}));
}

FK_TEST(SweepAlongHelixIsSpring) {
    // Molla: cerchio lungo l'elica esatta (V = pi r^2 L), con Frenet e con la torsione minima; quadrato con Frenet.
    HelixSpec spec;
    spec.frame = Frame3(Vec3(0, 0, 0), Vec3(0, 0, 1), Vec3(1, 0, 0));
    spec.radius = 4.0;
    spec.pitch = 1.5;
    spec.turns = 2.0;
    const auto helix = std::make_shared<HelixCurve>(spec);
    const std::vector<PathSegment> path{{helix, helix->domain()}};
    const double L = helix->length(), r = 0.5;
    const Frame3 start = normalFrame(helix->point(0.0), startTangent(path[0]));
    double volumes[2];
    for (int mode = 0; mode < 2; ++mode) {
        const Body spring = sweepRegions(start, {circleRegion(r)}, path, mode ? SweepOrientation::Frenet : SweepOrientation::MinimalTwist);
        const MassProperties m = checkedSolid(spring);
        volumes[mode] = m.volume;
        FK_CHECK_NEAR(m.volume, kPi * r * r * L, 1e-8 * m.volume);
        FK_CHECK_NEAR(m.area, kTwoPi * r * L + 2.0 * kPi * r * r, 1e-8 * m.area);
        // Il baricentro sta sull'asse a meta' altezza.
        FK_CHECK(std::hypot(m.centroid.x(), m.centroid.y()) < 1e-3);
    }
    FK_CHECK_NEAR(volumes[0], volumes[1], 1e-9 * volumes[0]);
    const MassProperties square = checkedSolid(sweepRegions(start, {squareRegion(0.6)}, path, SweepOrientation::Frenet));
    FK_CHECK_NEAR(square.volume, 0.36 * L, 1e-8 * 0.36 * L);
    // Riferimento OCCT: BRepOffsetAPI_MakePipe con Frenet sulla B-spline dell'elica.
    const BSplineCurve<3> spline = helixBSpline(*helix);
    const TopoDS_Wire spine = BRepBuilderAPI_MakeWire(BRepBuilderAPI_MakeEdge(toOcct(spline)).Edge()).Wire();
    BRepOffsetAPI_MakePipe pipe(spine, occtFace(circleRegion(r), start), GeomFill_IsFrenet);
    pipe.Build();
    FK_CHECK(pipe.IsDone());
    GProp_GProps props;
    BRepGProp::VolumeProperties(pipe.Shape(), props, 1e-9);
    FK_CHECK_NEAR(std::fabs(props.Mass()), volumes[1], 1e-5 * volumes[1]);
}

FK_TEST(SweepClosedPathAndSheet) {
    // Percorso chiuso: un cerchio intero (toro, niente facce di testa).
    const double R = 3.0, r = 0.5;
    const auto circle = std::make_shared<Circle<3>>(Vec3(1, 1, 0), Vec3(1, 0, 0), Vec3(0, 1, 0), R);
    const std::vector<PathSegment> ring{{circle, {0.0, kPi}}, {circle, {kPi, kTwoPi}}};
    const Body torus = sweepRegions(normalFrame(circle->point(0.0), startTangent(ring[0])), {circleRegion(r)}, ring);
    FK_CHECK_NEAR(checkedSolid(torus).volume, 2.0 * kPi * kPi * R * r * r, 1e-10);
    // Percorso chiuso liscio non circolare: un'ellisse (superfici B-spline).
    const auto loop = std::make_shared<Ellipse<3>>(Vec3(0, 0, 0), Vec3(1, 0, 0), Vec3(0, 1, 0), 5.0, 3.0);
    const std::vector<PathSegment> closed{{loop, {0.0, kPi}}, {loop, {kPi, kTwoPi}}};
    const Body band = sweepRegions(normalFrame(loop->point(0.0), startTangent(closed[0])), {circleRegion(0.3)}, closed);
    const double L = pathLength(closed);
    FK_CHECK_NEAR(checkedSolid(band).volume, kPi * 0.09 * L, 1e-8 * kPi * 0.09 * L);
    // Lamina: un segmento radiale lungo l'elica con Frenet e' un elicoide (area in forma chiusa).
    HelixSpec spec;
    spec.radius = 3.0;
    spec.pitch = 2.0;
    spec.turns = 1.5;
    const auto helix = std::make_shared<HelixCurve>(spec);
    const Vec3 p0 = helix->point(0.0);
    // Profilo nel piano (radiale, Z): il segmento va da raggio 2 a 4 lungo X.
    const Frame3 section(p0, Vec3(0, 1, 0), Vec3(1, 0, 0));
    ProfileLoop chain;
    chain.segments = {lineSegment(Vec2(-1, 0), Vec2(1, 0))};
    const Body strip = sweepChains(section, {chain}, {{helix, helix->domain()}}, SweepOrientation::Frenet);
    FK_CHECK(strip.isSheet());
    for (const CheckIssue &issue : checkBody(strip)) reportFailure(__FILE__, __LINE__, describe(issue.code) + ": " + issue.message);
    double area = 0.0;
    for (FaceId f : strip.faces()) area += faceArea(strip, f);
    const double c = spec.pitch / kTwoPi;
    const auto primitive = [c](double rho) { const double q = std::sqrt(rho * rho + c * c); return 0.5 * (rho * q + c * c * std::log(rho + q)); };
    FK_CHECK_NEAR(area, kTwoPi * spec.turns * (primitive(4.0) - primitive(2.0)), 1e-8 * area);
}
