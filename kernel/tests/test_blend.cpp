#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepFilletAPI_MakeChamfer.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <GProp_GProps.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>

#include "fk_blend.h"
#include "fk_body_check.h"
#include "fk_boolean.h"
#include "fk_extrude.h"
#include "fk_mass.h"
#include "fk_primitives.h"
#include "fk_tessellate.h"
#include "fk_test_profiles.h"

using namespace fktest;

namespace {

// Raccordo o smusso degli spigoli per i punti dati: body valido, volume
// atteso (se > 0) e visualizzazione riuscita. Restituisce il volume.
double blended(const Body &body, const std::vector<Vec3> &points, double size, bool chamfer, double expected) {
    std::vector<EdgeId> edges;
    for (const Vec3 &p : points) {
        const EdgeId e = nearestEdge(body, p, 1e-6);
        FK_CHECK(e.valid());
        if (!e.valid()) return 0.0;
        edges.push_back(e);
    }
    Body result;
    try {
        result = blendEdges(body, edges, size, chamfer);
    } catch (const std::exception &error) {
        reportFailure(__FILE__, __LINE__, std::string(chamfer ? "smusso " : "raccordo ") + std::to_string(points.front().x()) + " " + std::to_string(points.front().y()) + " " + std::to_string(points.front().z()) + ": " + error.what());
        return 0.0;
    }
    for (const CheckIssue &issue : checkBody(result)) reportFailure(__FILE__, __LINE__, describe(issue.code) + ": " + issue.message);
    const double volume = massProperties(result).volume;
    if (expected > 0.0) FK_CHECK_NEAR(volume, expected, 1e-9 * expected);
    TessellationOptions options;
    options.deflection = 0.01;
    FK_CHECK(tessellate(result, options).failedFaces == 0);
    return volume;
}

// Volume OCCT del parallelepipedo [0, a] x [0, b] x [0, c] con gli spigoli per i punti dati raccordati o smussati.
double occtBox(double a, double b, double c, const std::vector<Vec3> &points, double size, bool chamfer) {
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(a, b, c).Shape();
    TopTools_IndexedMapOfShape edges;
    TopExp::MapShapes(box, TopAbs_EDGE, edges);
    auto nearest = [&](const Vec3 &p) {
        const TopoDS_Shape vertex = BRepBuilderAPI_MakeVertex(gp_Pnt(p.x(), p.y(), p.z())).Shape();
        int best = 1;
        double closest = 1e300;
        for (int i = 1; i <= edges.Extent(); ++i) {
            BRepExtrema_DistShapeShape distance(vertex, edges(i));
            if (distance.Value() < closest) {
                closest = distance.Value();
                best = i;
            }
        }
        return TopoDS::Edge(edges(best));
    };
    TopoDS_Shape result;
    if (chamfer) {
        BRepFilletAPI_MakeChamfer maker(box);
        for (const Vec3 &p : points) maker.Add(size, nearest(p));
        result = maker.Shape();
    } else {
        BRepFilletAPI_MakeFillet maker(box);
        for (const Vec3 &p : points) maker.Add(size, nearest(p));
        result = maker.Shape();
    }
    GProp_GProps props;
    BRepGProp::VolumeProperties(result, props, 1e-12);
    return props.Mass();
}

// Volume del solido generato ruotando attorno all'asse la zona tra un angolo
// retto (spigolo in (rho, z)) e il raccordo di raggio r (Pappus).
double ringVolume(double rho, double r, bool inward) {
    // Quadrato r x r meno il quarto di cerchio: baricentri radiali dal lato del centro del cerchio.
    const double side = inward ? -1.0 : 1.0;  // il centro del cerchio sta a rho + side r
    const double square = r * r, quarter = kPi * r * r / 4.0;
    const double squareRho = rho + side * r / 2.0, quarterRho = rho + side * (r - 4.0 * r / (3.0 * kPi));
    return kTwoPi * (square * squareRho - quarter * quarterRho);
}

}

FK_TEST(BlendBoxEdges) {
    const double a = 10, b = 6, c = 4, r = 1.0;
    const Body box = makeBox(Frame3(), a, b, c);
    const double v = a * b * c;
    // Un raccordo e uno smusso sullo spigolo superiore lungo x.
    blended(box, {Vec3(5, 0, 4)}, r, false, v - (1.0 - kPi / 4.0) * r * r * a);
    blended(box, {Vec3(5, 0, 4)}, r, true, v - 0.5 * r * r * a);
    FK_CHECK_NEAR(occtBox(a, b, c, {Vec3(5, 0, 4)}, r, false), v - (1.0 - kPi / 4.0) * r * r * a, 1e-6);
    // Due spigoli che si incontrano in un vertice: come OCCT.
    for (bool chamfer : {false, true}) {
        const std::vector<Vec3> two{Vec3(5, 0, 4), Vec3(10, 3, 4)};
        const double ours = blended(box, two, r, chamfer, 0.0);
        FK_CHECK_NEAR(ours, occtBox(a, b, c, two, r, chamfer), 1e-6);
    }
    // Spigoli paralleli e uno verticale.
    const std::vector<Vec3> apart{Vec3(5, 0, 4), Vec3(5, 6, 0)};
    blended(box, apart, 0.5, false, v - (1.0 - kPi / 4.0) * 0.25 * (a + a));
    // Tre spigoli in un vertice: la pezza d'angolo non e' gestita.
    FK_CHECK_THROWS(blendEdges(box, {nearestEdge(box, Vec3(5, 0, 4), 1e-6), nearestEdge(box, Vec3(10, 3, 4), 1e-6), nearestEdge(box, Vec3(10, 0, 2), 1e-6)}, r, false));
}

FK_TEST(BlendConcaveAndCurvedEdges) {
    // Profilo a L estruso: lo spigolo interno (concavo) riceve materiale.
    const double h = 5.0, r = 0.8;
    const Body ell = makeExtrusion(Frame3(), buildProfile({lineSegment(Vec2(0, 0), Vec2(6, 0)), lineSegment(Vec2(6, 0), Vec2(6, 2)),
                                                           lineSegment(Vec2(6, 2), Vec2(2, 2)), lineSegment(Vec2(2, 2), Vec2(2, 5)),
                                                           lineSegment(Vec2(2, 5), Vec2(0, 5)), lineSegment(Vec2(0, 5), Vec2(0, 0))}, 1e-9)
                                                  .regions.front(), h);
    const double vEll = (12.0 + 6.0) * h;
    blended(ell, {Vec3(2, 2, 2.5)}, r, false, vEll + (1.0 - kPi / 4.0) * r * r * h);
    blended(ell, {Vec3(2, 2, 2.5)}, r, true, vEll + 0.5 * r * r * h);
    // Cilindro: bordo superiore (toro) e smusso (cono).
    const double R = 3.0, H = 4.0, q = 0.5;
    const Body cylinder = makeCylinder(Frame3(Vec3(1, 2, 3), Vec3(0, 0, 1), Vec3(1, 0, 0)), R, H);
    const double vCyl = kPi * R * R * H;
    blended(cylinder, {Vec3(1 + R, 2, 3 + H)}, q, false, vCyl - ringVolume(R, q, true));
    blended(cylinder, {Vec3(1 + R, 2, 3 + H)}, q, true, vCyl - kTwoPi * (R - q / 3.0) * q * q / 2.0);
    // Foro in un blocco: il bordo del foro (convesso) e il cilindro con un
    // bordo piano (profilo a D: spigolo rettilineo tra piano e cilindro).
    const Body block = booleanOperation(makeBox(Frame3(), 10, 10, 4), makeCylinder(Frame3(Vec3(5, 5, -1), Vec3(0, 0, 1), Vec3(1, 0, 0)), 2.0, 6.0),
                                        BooleanOperation::Subtract);
    blended(block, {Vec3(7, 5, 4)}, q, false, 400.0 - kPi * 4.0 * 4.0 - ringVolume(2.0, q, false));
    // Perno unito sopra un blocco: lo spigolo alla base (concavo, circolare) riceve un raccordo.
    const Body boss = booleanOperation(makeBox(Frame3(), 10, 10, 4), makeCylinder(Frame3(Vec3(5, 5, 4), Vec3(0, 0, 1), Vec3(1, 0, 0)), 2.0, 3.0),
                                       BooleanOperation::Unite);
    blended(boss, {Vec3(7, 5, 4)}, q, false, 400.0 + kPi * 4.0 * 3.0 + ringVolume(2.0, q, false));
    const Body dee = makeExtrusion(Frame3(), buildProfile({arcSegment(Vec2(0, 0), 3.0, -kHalfPi, kHalfPi), lineSegment(Vec2(0, 3), Vec2(0, -3))}, 1e-9)
                                                 .regions.front(), 4.0);
    const double vDee = 0.5 * kPi * 9.0 * 4.0;
    const double ours = blended(dee, {Vec3(0, 3, 2)}, q, false, 0.0);
    FK_CHECK(ours < vDee && ours > vDee - q * q * 4.0);
}
