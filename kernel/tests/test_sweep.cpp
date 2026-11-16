#include <BRepGProp.hxx>
#include <BRepOffsetAPI_MakePipe.hxx>
#include <GProp_GProps.hxx>

#include <cmath>

#include "fk_body_check.h"
#include "fk_curve_algo.h"
#include "fk_curve_ops.h"
#include "fk_helix.h"
#include "fk_mass.h"
#include "fk_nurbs.h"
#include "fk_sweep.h"
#include "fk_pcurve.h"
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

FK_TEST(SweepMinimalTwistJoinedSpatialPathWithStraightSpans) {
    // Stessa geometria come catena analitica e come un'unica NURBS razionale.
    // Due pieghe in piani diversi, separate da rettilinei: Frenet non esiste.
    const auto first = std::make_shared<Circle<3>>(Vec3(2, 4, 0), Vec3(0, -1, 0), Vec3(1, 0, 0), 4.0);
    const auto second = std::make_shared<Circle<3>>(Vec3(6, 6, 4), Vec3(0, 0, -1), Vec3(0, 1, 0), 4.0);
    const std::vector<PathSegment> path{
        {std::make_shared<Line<3>>(Vec3(0, 0, 0), Vec3(1, 0, 0)), {0, 2}}, {first, {0, kHalfPi}},
        {std::make_shared<Line<3>>(Vec3(6, 4, 0), Vec3(0, 1, 0)), {0, 2}}, {second, {0, kHalfPi}},
        {std::make_shared<Line<3>>(Vec3(6, 10, 4), Vec3(0, 0, 1)), {0, 2}}};
    std::vector<BSplineCurve<3>> pieces;
    for (const auto &segment : path) {
        const auto parts = standardBezierPieces(*segment.curve, segment.range, 3);
        pieces.insert(pieces.end(), parts.begin(), parts.end());
    }
    const auto joined = std::make_shared<BSplineCurve<3>>(joinBezierPieces(pieces));
    const Frame3 start(Vec3(0, 0, 0), Vec3(1, 0, 0), Vec3(0, 1, 0));
    const auto profile = squareRegion(0.4);
    const Body reference = sweepRegions(start, {profile}, path);
    const Body solid = sweepRegions(start, {profile}, {{joined, joined->domain()}});
    const auto mass = checkedSolid(solid);
    FK_CHECK_NEAR(mass.volume, 0.16 * (6.0 + 4.0 * kPi), 1e-7);
    FK_CHECK_NEAR(mass.volume, checkedSolid(reference).volume, 1e-7);
    // Il quadrato rende visibile la rotazione: confronta i vertici finali,
    // non soltanto volume o sezione circolare invarianti rispetto alla torsione.
    int endVertices = 0;
    for (auto v : solid.vertices()) if (std::abs(solid.vertex(v).point.z() - 6.0) < 1e-8) {
        ++endVertices;
        double gap = 1e100;
        for (auto w : reference.vertices()) gap = std::min(gap, distance(solid.vertex(v).point, reference.vertex(w).point));
        FK_CHECK(gap < 1e-8);
    }
    FK_CHECK(endVertices == 4);
    FK_CHECK_THROWS(sweepRegions(start, {profile}, {{joined, joined->domain()}}, SweepOrientation::Frenet));
}

FK_TEST(SweepMinimalTwistSpatialInflection) {
    // P(u)=(t,t^3,t^4), t=u-1/2: tangente regolare e curvatura nulla
    // all'interno, su una curva che non giace in un piano.
    const auto spline = std::make_shared<BSplineCurve<3>>(4, std::vector<double>{0,0,0,0,0,1,1,1,1,1},
        std::vector<Vec3>{{-.5,-.125,.0625},{-.25,.0625,-.0625},{0,0,.0625},{.25,-.0625,-.0625},{.5,.125,.0625}});
    const std::vector<PathSegment> path{{spline, {0,1}}};
    const Frame3 start = normalFrame(spline->point(0), normalized(spline->derivative(0)));
    const Body solid = sweepRegions(start, {circleRegion(.01)}, path);
    FK_CHECK_NEAR(checkedSolid(solid).volume, kPi * .0001 * pathLength(path), 1e-10);
    FK_CHECK_THROWS(sweepRegions(start, {circleRegion(.01)}, path, SweepOrientation::Frenet));
}

FK_TEST(SweepMinimalTwistClosedSpatialSpline) {
    const auto spline = std::make_shared<BSplineCurve<3>>(3,
        std::vector<double>{0,0,0,0,.2,.4,.6,.8,1,1,1,1},
        std::vector<Vec3>{{4,0,0},{4,3,0},{0,5,1},{-4,3,-1},{-4,-3,1},{0,-5,-1},{4,-3,0},{4,0,0}});
    const std::vector<PathSegment> path{{spline,{0,.5}},{spline,{.5,1}}};
    const Frame3 start = normalFrame(spline->point(0), spline->derivative(0));
    const Body solid = sweepRegions(start, {circleRegion(.1)}, path);
    FK_CHECK_NEAR(checkedSolid(solid).volume, kPi * .01 * pathLength(path), 1e-8);
    for (auto edge : solid.edges()) FK_CHECK(!solid.isLaminar(edge));
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

FK_TEST(SweepObliqueProfileAlongArcUsesFiniteMeridians) {
    // Un profilo non perfettamente normale al percorso produce, per alcuni
    // lati rettilinei, superfici di rivoluzione generali. La retta di
    // supporto e' infinita, ma il meridiano della faccia deve restare il
    // segmento finito dello schizzo.
    const double radius = 3.0;
    const auto arc = std::make_shared<Circle<3>>(Vec3(0, 0, 0), Vec3(1, 0, 0), Vec3(0, 1, 0), radius);
    const std::vector<PathSegment> path{{arc, {0.0, 0.8}}};
    const Frame3 oblique(arc->point(0.0), normalized(Vec3(0, 1, 0.2)), Vec3(1, 0, 0));
    const Body body = sweepRegions(oblique, {squareRegion(0.4)}, path, SweepOrientation::MinimalTwist);
    FK_CHECK(checkedSolid(body).volume > 0.0);
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

FK_TEST(SweepMinimalTwistHelixAvoidsUniformGridAliasing) {
    // 16 turns put all 17 old classification samples on one generator.
    // Adjacent turn counts and a rigid pose guard against a grid-specific fix.
    for (double turns : {15.0, 16.0, 17.0}) {
        HelixSpec spec;
        spec.radius = 4.0;
        spec.pitch = 1.0;
        spec.turns = turns;
        if (turns == 17.0)
            spec.frame = Frame3(Vec3(12, -7, 3), normalized(Vec3(1, 2, 3)), Vec3(0, 1, 0));
        const auto helix = std::make_shared<HelixCurve>(spec);
        const Frame3 section = normalFrame(helix->point(0), helix->derivative(0));
        const Body tube = sweepRegions(section, {circleRegion(0.2)}, {{helix, helix->domain()}}, SweepOrientation::MinimalTwist);
        FK_CHECK(checkBody(tube).empty());
        const MassProperties m = massProperties(tube, 1e-11);
        const double expected = kPi * 0.04 * helix->length();
        FK_CHECK_NEAR(m.volume, expected, expected * 1e-8);
        FK_CHECK_NEAR(m.area, kTwoPi * 0.2 * helix->length() + 2 * kPi * 0.04, m.area * 1e-8);
    }
}


FK_TEST(SweepClosedSpatialSplineExplicitSeamParameters) {
    const auto spline = std::make_shared<BSplineCurve<3>>(3,
        std::vector<double>{0,0,0,0,.2,.4,.6,.8,1,1,1,1},
        std::vector<Vec3>{{4,0,0},{4,3,0},{0,5,1},{-4,3,-1},{-4,-3,1},{0,-5,-1},{4,-3,0},{4,0,0}});
    const Frame3 start = normalFrame(spline->point(0), spline->derivative(0));
    const double length = arcLength(*spline, {0,1}, 1e-13);
    for (const auto &profile : {circleRegion(.1), squareRegion(.2)}) {
        const Body whole = sweepRegions(start, {profile}, {{spline,{0,1}}});
        const Body split = sweepRegions(start, {profile}, {{spline,{0,.5}},{spline,{.5,1}}});
        const auto a = checkedSolid(whole), b = checkedSolid(split);
        // Budget deliberatamente sopra il limite 1e-8 della quadratura:
        // il fallback deve rifinire dall'SP-curve e conservarne il ramo.
        Body loose = whole;
        for (FaceId f : loose.faces()) for (LoopId l : loose.face(f).loops)
            for (FinId fin : loose.loopFins(l))
                loose.fin(fin).pcurveTolerance = std::max(loose.fin(fin).pcurveTolerance, 2e-8);
        const auto refined = massProperties(loose, 1e-11);
        FK_CHECK_NEAR(refined.volume, a.volume, 1e-8);
        FK_CHECK_NEAR(refined.area, a.area, 1e-7);
        FK_CHECK_NEAR(a.volume, b.volume, 1e-8);
        FK_CHECK_NEAR(a.area, b.area, 1e-7);
        int seams = 0;
        for (FaceId f : whole.faces()) {
            const Surface &surface = *whole.face(f).surface;
            if (surface.type() != SurfaceType::BSpline) continue;
            for (LoopId loop : whole.face(f).loops) {
                const auto fins = whole.loopFins(loop);
                for (std::size_t i = 0; i < fins.size(); ++i) {
                    const Fin &fin = whole.fin(fins[i]);
                    const Fin &next = whole.fin(fins[(i + 1) % fins.size()]);
                    const Edge &edge = whole.edge(fin.edge), &nextEdge = whole.edge(next.edge);
                    FK_CHECK(bool(fin.pcurve)); FK_CHECK(bool(next.pcurve));
                    if (!fin.pcurve || !next.pcurve) continue;
                    const Vec2 end = fin.pcurve->point(fin.sense ? edge.range.hi : edge.range.lo);
                    const Vec2 begin = next.pcurve->point(next.sense ? nextEdge.range.lo : nextEdge.range.hi);
                    FK_CHECK(distance(end, begin) < 1e-10);
                    FK_CHECK(pcurveDeviation(surface, *edge.curve, *fin.pcurve, edge.range) <=
                        std::max(linearResolution(), edge.tolerance) + fin.pcurveTolerance);
                    for (std::size_t j = i + 1; j < fins.size(); ++j) {
                        const Fin &other = whole.fin(fins[j]);
                        if (other.edge != fin.edge || !other.pcurve) continue;
                        ++seams;
                        const double t = .5 * (edge.range.lo + edge.range.hi);
                        const double v = fin.pcurve->point(t).y(), w = other.pcurve->point(t).y();
                        FK_CHECK_NEAR(std::abs(v - w), surface.vDomain().length(), 1e-12);
                    }
                }
            }
        }
        FK_CHECK(seams > 0);
    }
    const auto disk = checkedSolid(sweepRegions(start, {circleRegion(.1)}, {{spline,{0,1}}}));
    FK_CHECK_NEAR(disk.volume, kPi * .01 * length, 1e-8);
    FK_CHECK_NEAR(disk.area, kTwoPi * .1 * length, 1e-7);
    const auto open = checkedSolid(sweepRegions(start, {circleRegion(.1)}, {{spline,{0,.75}}}));
    FK_CHECK_NEAR(open.volume, kPi * .01 * arcLength(*spline,{0,.75},1e-13), 1e-8);
}

FK_TEST(SweepLocalFoldUsesSectionGeometry) {
    // A rotated circle has NURBS poles outside its true radius. Those poles
    // may cross the curvature axis while the entire circle remains valid.
    for (double scale : {0.1, 1.0, 10.0}) for (int rotated = 0; rotated < 2; ++rotated) {
        const Frame3 pose(Vec3(13, -7, 5), normalized(Vec3(1, 2, 3)), Vec3(0, 1, 0));
        const double R = 10 * scale, r = 8 * scale;
        const auto pathCircle = std::make_shared<Circle<3>>(pose.origin(), pose.xDir(), pose.yDir(), R);
        const Interval range{0, kHalfPi};
        const auto pathNurbs = std::make_shared<BSplineCurve<3>>(toBSpline(*pathCircle, range.lo, range.hi));
        const Frame3 section(pathCircle->point(0), pathCircle->derivative(0),
            rotated ? normalized(pose.xDir() + pose.zDir()) : pose.xDir());
        for (int representation = 0; representation < 3; ++representation) {
            ProfileRegion disk = circleRegion(r);
            CurvePtr<3> path = representation ? std::static_pointer_cast<const Curve<3>>(pathNurbs) : pathCircle;
            if (representation == 2) {
                const Circle<2> circle = makeCircle(Vec2(), r);
                const auto c = std::make_shared<BSplineCurve<2>>(toBSpline(circle, 0, kTwoPi));
                disk.outer.segments = {{c, c->domain()}};
            }
            const Body tube = sweepRegions(section, {disk}, {{path, range}});
            FK_CHECK(checkBody(tube).empty());
            const double expected = kPi * r * r * R * kHalfPi;
            FK_CHECK_NEAR(massProperties(tube).volume, expected, expected * 2e-8);
        }
        // A genuine fold stays rejected, independently of representation.
        for (CurvePtr<3> path : {std::static_pointer_cast<const Curve<3>>(pathCircle),
                                std::static_pointer_cast<const Curve<3>>(pathNurbs)})
            FK_CHECK_THROWS(sweepRegions(section, {circleRegion(11 * scale)}, {{path, range}}));
    }
}
