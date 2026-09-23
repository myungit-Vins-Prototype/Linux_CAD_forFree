#include <BRepAdaptor_Curve.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRep_Tool.hxx>
#include <GCPnts_AbscissaPoint.hxx>
#include <GProp_GProps.hxx>
#include <Geom_Circle.hxx>
#include <Geom_Ellipse.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <gp_Ax2.hxx>
#include <gp_Pln.hxx>

#include "fk_body_check.h"
#include "fk_curve_algo.h"
#include "fk_curve_ops.h"
#include "fk_extrude.h"
#include "fk_test_profiles.h"

using namespace fktest;

namespace {

// Area con integrazione adattiva (Eps). Senza Eps BRepGProp usa una regola di
// Gauss a ordine fisso che sulle curve razionali sbaglia gia' alla quarta
// cifra; e VolumeProperties, anche con Eps, sulle facce estruse da spline
// sbaglia di parti per mille. Per un prisma il volume e' area x altezza.
double occtArea(const TopoDS_Face &face) {
    GProp_GProps properties;
    BRepGProp::SurfaceProperties(face, properties, 1e-12);
    return std::fabs(properties.Mass());
}

double occtLength(const TopoDS_Edge &edge) {
    BRepAdaptor_Curve curve(edge);
    return GCPnts_AbscissaPoint::Length(curve, 1e-12);
}

// Lunghezza degli spigoli di OCCT senza le cuciture (che noi non abbiamo).
double occtEdgeLengthWithoutSeams(const TopoDS_Shape &shape) {
    TopTools_IndexedMapOfShape seams;
    for (TopExp_Explorer faces(shape, TopAbs_FACE); faces.More(); faces.Next())
        for (TopExp_Explorer edges(faces.Current(), TopAbs_EDGE); edges.More(); edges.Next())
            if (BRep_Tool::IsClosed(TopoDS::Edge(edges.Current()), TopoDS::Face(faces.Current()))) seams.Add(edges.Current());
    TopTools_IndexedMapOfShape edges;
    TopExp::MapShapes(shape, TopAbs_EDGE, edges);
    double length = 0.0;
    for (int i = 1; i <= edges.Extent(); ++i) {
        if (seams.Contains(edges(i))) continue;
        length += occtLength(TopoDS::Edge(edges(i)));
    }
    return length;
}

int countShapes(const TopoDS_Shape &shape, TopAbs_ShapeEnum type) {
    TopTools_IndexedMapOfShape map;
    TopExp::MapShapes(shape, type, map);
    return map.Extent();
}

double totalEdgeLength(const Body &body) {
    double length = 0.0;
    for (EdgeId e : body.edges()) length += arcLength(*body.edge(e).curve, body.edge(e).range);
    return length;
}

bool valid(const Body &body) {
    const std::vector<CheckIssue> issues = checkBody(body);
    for (const CheckIssue &issue : issues) reportFailure(__FILE__, __LINE__, describe(issue.code) + ": " + issue.message);
    return issues.empty();
}

// Estrusione del nuovo kernel confrontata con quella di OCCT.
void compareExtrusion(const ProfileRegion &region, const Frame3 &frame, double height) {
    const Body body = makeExtrusion(frame, region, height);
    FK_CHECK(valid(body));
    int segments = int(region.outer.segments.size()), closedLoops = region.outer.segments.size() == 1;
    for (const ProfileLoop &hole : region.holes) {
        segments += int(hole.segments.size());
        closedLoops += hole.segments.size() == 1;
    }
    const TopologyCounts c = body.counts();
    FK_CHECK(c.faces == segments + 2);
    FK_CHECK(c.edges == 3 * segments - closedLoops);  // niente spigoli verticali sui loop chiusi
    FK_CHECK(shellGenus(body, body.shells().front()) == int(region.holes.size()));

    const TopoDS_Face base = occtFace(region, frame);
    const TopoDS_Shape reference = BRepPrimAPI_MakePrism(base, gp_Vec(toPnt(Vec3()), toPnt(height * frame.zDir()))).Shape();
    FK_CHECK(countShapes(reference, TopAbs_FACE) == c.faces);
    FK_CHECK(countShapes(reference, TopAbs_VERTEX) == c.vertices);
    FK_CHECK(countShapes(reference, TopAbs_EDGE) == c.edges + closedLoops);  // OCCT: una cucitura per loop chiuso
    const double baseArea = occtArea(base);
    FK_CHECK_NEAR(area(region), baseArea, 1e-9 * baseArea);
    const double length = occtEdgeLengthWithoutSeams(reference);
    FK_CHECK_NEAR(totalEdgeLength(body), length, 1e-8 * length);
}

}

FK_TEST(ProfileAreaAndWinding) {
    ProfileLoop circle{{arcSegment(Vec2(1, 2), 3.0, 0.0, kTwoPi)}};
    FK_CHECK_NEAR(signedArea(circle), kPi * 9.0, 1e-12);
    FK_CHECK_NEAR(signedArea(reversed(circle)), -kPi * 9.0, 1e-12);
    FK_CHECK(windingNumber(circle, Vec2(1.5, 2.5)) == 1);
    FK_CHECK(windingNumber(reversed(circle), Vec2(1.5, 2.5)) == -1);
    FK_CHECK(windingNumber(circle, Vec2(10, 2)) == 0);
    ProfileLoop rounded{roundedRectangle(Vec2(-1, -2), 8.0, 5.0, 1.5)};
    FK_CHECK_NEAR(signedArea(rounded), 40.0 - (4.0 - kPi) * 2.25, 1e-12);
    // Area di una B-spline chiusa contro quella della faccia di OCCT.
    const ProfileLoop blob{{closedSpline(Vec2(3, -1), 2.0, true)}};
    GProp_GProps properties;
    BRepGProp::SurfaceProperties(BRepBuilderAPI_MakeFace(occtWire(blob, Frame3())).Face(), properties, 1e-12);
    FK_CHECK_NEAR(std::fabs(signedArea(blob)), std::fabs(properties.Mass()), 1e-10);
}

FK_TEST(ProfileChainingAndNesting) {
    std::mt19937 rng(100);
    for (int trial = 0; trial < 50; ++trial) {
        // Contorno arrotondato, foro circolare con un'isola (spline) dentro, foro a spline; piu' una linea aperta.
        std::vector<ProfileSegment> segments = roundedRectangle(Vec2(-10, -10), 20.0, 20.0, 3.0);
        segments.push_back(arcSegment(Vec2(-4, 0), 4.0, 0.0, kTwoPi));
        segments.push_back(closedSpline(Vec2(-4, 0), 1.5, false));
        segments.push_back(closedSpline(Vec2(5, 4), 2.0, true));
        segments.push_back(lineSegment(Vec2(30, 0), Vec2(31, 1)));
        // Ordine e verso casuali, estremi disturbati entro la tolleranza.
        std::shuffle(segments.begin(), segments.end(), rng);
        for (ProfileSegment &segment : segments)
            if (uniformInt(rng, 0, 1)) segment = reversed(segment);
        const Profile profile = buildProfile(segments, 1e-6);
        FK_CHECK(profile.openChains == 1);
        FK_CHECK(profile.regions.size() == 2);
        if (profile.regions.size() != 2) continue;
        const ProfileRegion &big = std::fabs(signedArea(profile.regions[0].outer)) > std::fabs(signedArea(profile.regions[1].outer))
            ? profile.regions[0] : profile.regions[1];
        const ProfileRegion &island = &big == &profile.regions[0] ? profile.regions[1] : profile.regions[0];
        FK_CHECK(big.outer.segments.size() == 8 && big.holes.size() == 2);
        FK_CHECK(island.holes.empty() && island.outer.segments.size() == 1);
        FK_CHECK(signedArea(big.outer) > 0.0 && signedArea(island.outer) > 0.0);
        for (const ProfileLoop &hole : big.holes) FK_CHECK(signedArea(hole) < 0.0);
        // Loop concatenati: la fine di ogni tratto e' l'inizio del successivo.
        for (std::size_t i = 0; i < big.outer.segments.size(); ++i)
            FK_CHECK(distance(big.outer.segments[i].end(), big.outer.segments[(i + 1) % 8].start()) <= 1e-9);
    }
}

FK_TEST(ExtrusionMatchesOcct) {
    std::mt19937 rng(101);
    for (int trial = 0; trial < 24; ++trial) {
        const Frame3 frame = randomFrame(rng, 50.0);
        const double height = uniform(rng, 0.5, 30.0) * (trial % 3 == 0 ? -1.0 : 1.0);
        std::vector<ProfileSegment> segments;
        switch (trial % 6) {
        case 0:  // cilindro: un solo cerchio
            segments = {arcSegment(Vec2(1, 1), uniform(rng, 0.5, 10.0), 0.0, kTwoPi)};
            break;
        case 1:  // "D": un segmento e un arco (due tratti)
            segments = {lineSegment(Vec2(0, -3), Vec2(0, 3)), arcSegment(Vec2(), 3.0, kHalfPi, 1.5 * kPi)};
            break;
        case 2:  // rettangolo arrotondato con foro circolare e foro a spline
            segments = roundedRectangle(Vec2(-10, -6), 20.0, 12.0, 2.5);
            segments.push_back(arcSegment(Vec2(-4, 0), 2.0, 0.0, kTwoPi));
            segments.push_back(closedSpline(Vec2(4, 0), 2.0, false));
            break;
        case 3:  // NURBS chiusa (razionale) con un foro ellittico
            segments = {closedSpline(Vec2(), 8.0, true),
                        {std::make_shared<Ellipse<2>>(makeEllipse(Vec2(0.5, 0.3), 2.0, 1.0)), {0.0, kTwoPi}}};
            break;
        case 4: {  // spline aperta chiusa da due segmenti
            auto spline = std::make_shared<BSplineCurve<2>>(
                3, std::vector<double>{0, 0, 0, 0, 0.5, 1, 1, 1, 1},
                std::vector<Vec2>{Vec2(-5, 0), Vec2(-4, 4), Vec2(0, 6), Vec2(4, 2), Vec2(5, 0)});
            segments = {{spline, spline->domain()}, lineSegment(Vec2(5, 0), Vec2(0, -3)), lineSegment(Vec2(0, -3), Vec2(-5, 0))};
            break;
        }
        default:  // arco di ellisse e segmento, dentro un contorno quadrato come foro
            segments = {lineSegment(Vec2(-9, -9), Vec2(9, -9)), lineSegment(Vec2(9, -9), Vec2(9, 9)),
                        lineSegment(Vec2(9, 9), Vec2(-9, 9)), lineSegment(Vec2(-9, 9), Vec2(-9, -9)),
                        {std::make_shared<Ellipse<2>>(makeEllipse(Vec2(), 4.0, 2.0)), {0.0, kPi}},
                        lineSegment(Vec2(-4, 0), Vec2(4, 0))};
            break;
        }
        std::shuffle(segments.begin(), segments.end(), rng);
        const Profile profile = buildProfile(segments, 1e-6);
        FK_CHECK(profile.regions.size() == 1 && profile.openChains == 0);
        if (profile.regions.size() == 1) compareExtrusion(profile.regions.front(), frame, height);
    }
}

// Estremi che si toccano solo entro la tolleranza dello schizzo: vertici tolleranti.
FK_TEST(ExtrusionWithTolerantVertices) {
    const double gap = 5e-7;
    std::vector<ProfileSegment> segments = {lineSegment(Vec2(0, 0), Vec2(10, 0)), lineSegment(Vec2(10, gap), Vec2(10, 10)),
                                            lineSegment(Vec2(10, 10), Vec2(0, 10)), lineSegment(Vec2(0, 10), Vec2(0, 0))};
    const Profile profile = buildProfile(segments, 1e-6);
    FK_CHECK(profile.regions.size() == 1);
    const Body body = makeExtrusion(Frame3(), profile.regions.front(), 2.0);
    FK_CHECK(valid(body));
    int tolerant = 0;
    for (VertexId v : body.vertices()) tolerant += body.vertex(v).tolerance > 0.0;
    FK_CHECK(tolerant == 2);  // il vertice interessato, alla base e in cima
}
