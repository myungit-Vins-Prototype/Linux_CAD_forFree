#include <cmath>
#include <TopExp_Explorer.hxx>
#include <Poly_Triangulation.hxx>
#include <BRep_Tool.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <cstdio>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepFilletAPI_MakeChamfer.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <GProp_GProps.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>

#include "fk_blend.h"
#include "fk_blend_loop.h"
#include "fk_curve_algo.h"
#include "fk_curve_ops.h"
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

// Solido del nuovo kernel e lo stesso in OCCT (estrusione del profilo lungo Z).
struct Operand {
    Body body;
    TopoDS_Shape shape;
};

Operand extrusion(const std::vector<ProfileSegment> &segments, double height) {
    const ProfileRegion region = buildProfile(segments, 1e-9).regions.front();
    return {makeExtrusion(Frame3(), region, height),
            BRepPrimAPI_MakePrism(occtFace(region, Frame3()), gp_Vec(0.0, 0.0, height)).Shape()};
}

std::vector<ProfileSegment> polygon(const std::vector<Vec2> &points) {
    std::vector<ProfileSegment> segments;
    for (std::size_t i = 0; i < points.size(); ++i) segments.push_back(lineSegment(points[i], points[(i + 1) % points.size()]));
    return segments;
}

// Forma OCCT con gli spigoli per i punti dati raccordati o smussati (nulla se OCCT fallisce).
TopoDS_Shape occtBlendedShape(const TopoDS_Shape &shape, const std::vector<Vec3> &points, double size, bool chamfer) {
    TopTools_IndexedMapOfShape edges;
    TopExp::MapShapes(shape, TopAbs_EDGE, edges);
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
    try {
        if (chamfer) {
            BRepFilletAPI_MakeChamfer maker(shape);
            for (const Vec3 &p : points) maker.Add(size, nearest(p));
            return maker.Shape();
        }
        BRepFilletAPI_MakeFillet maker(shape);
        for (const Vec3 &p : points) maker.Add(size, nearest(p));
        return maker.Shape();
    } catch (const Standard_Failure &) {
        return {};
    }
}

// Volume OCCT (BRepGProp adattivo) della forma raccordata, 0 se OCCT fallisce.
double occtBlended(const TopoDS_Shape &shape, const std::vector<Vec3> &points, double size, bool chamfer) {
    const TopoDS_Shape result = occtBlendedShape(shape, points, size, chamfer);
    if (result.IsNull()) return 0.0;
    GProp_GProps props;
    BRepGProp::VolumeProperties(result, props, 1e-12);
    return props.Mass();
}

// Volume della tassellazione fine di OCCT: il terzo parere sui prismi estrusi
// da spline, dove BRepGProp sbaglia anche dell'1% (vedi CLAUDE.md).
double meshVolume(const TopoDS_Shape &shape) {
    BRepMesh_IncrementalMesh mesh(shape, 2e-4, false, 0.02);
    double volume = 0.0;
    for (TopExp_Explorer faces(shape, TopAbs_FACE); faces.More(); faces.Next()) {
        const TopoDS_Face &face = TopoDS::Face(faces.Current());
        TopLoc_Location location;
        const Handle(Poly_Triangulation) triangulation = BRep_Tool::Triangulation(face, location);
        if (triangulation.IsNull()) continue;
        for (int t = 1; t <= triangulation->NbTriangles(); ++t) {
            int n1, n2, n3;
            triangulation->Triangle(t).Get(n1, n2, n3);
            if (face.Orientation() == TopAbs_REVERSED) std::swap(n2, n3);
            const gp_Pnt p1 = triangulation->Node(n1).Transformed(location), p2 = triangulation->Node(n2).Transformed(location),
                         p3 = triangulation->Node(n3).Transformed(location);
            volume += gp_Vec(p1.XYZ()).Dot(gp_Vec(p2.XYZ()).Crossed(gp_Vec(p3.XYZ()))) / 6.0;
        }
    }
    return volume;
}

// Volume tolto (o aggiunto, negativo) da OCCT secondo le tassellazioni fini, NaN se OCCT fallisce.
double occtRemoved(const TopoDS_Shape &shape, const std::vector<Vec3> &points, double size, bool chamfer) {
    const TopoDS_Shape result = occtBlendedShape(shape, points, size, chamfer);
    if (result.IsNull()) return std::nan("");
    return meshVolume(shape) - meshVolume(result);
}

// Punti medi degli edge di una faccia piana a quota z (per scegliere tutti i suoi bordi).
std::vector<Vec3> edgesAtHeight(const Body &body, double z) {
    std::vector<Vec3> points;
    for (EdgeId e : body.edges()) {
        const Edge &edge = body.edge(e);
        const Vec3 p = edge.curve->point(0.5 * (edge.range.lo + edge.range.hi));
        if (std::fabs(p.z() - z) < 1e-9 && std::fabs(edge.curve->point(edge.range.lo).z() - z) < 1e-9) points.push_back(p);
    }
    return points;
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
    // Tre spigoli in un vertice: la pezza d'angolo (ottante di sfera per il
    // raccordo, triangolo per lo smusso), come OCCT.
    const std::vector<Vec3> three{Vec3(5, 0, 4), Vec3(10, 3, 4), Vec3(10, 0, 2)};
    const double corner = (1.0 - kPi / 4.0) * r * r * ((a - r) + (b - r) + (c - r)) + r * r * r * (1.0 - kPi / 6.0);
    blended(box, three, r, false, v - corner);
    FK_CHECK_NEAR(occtBox(a, b, c, three, r, false), v - corner, 1e-6);
    FK_CHECK_NEAR(blended(box, three, r, true, 0.0), occtBox(a, b, c, three, r, true), 1e-6);
    // Tutti gli spigoli: otto angoli.
    std::vector<Vec3> all;
    for (EdgeId e : box.edges()) all.push_back(box.edge(e).curve->point(0.5 * (box.edge(e).range.lo + box.edge(e).range.hi)));
    const double q = 0.75;
    const double rounded = v - (1.0 - kPi / 4.0) * q * q * 4.0 * ((a - 2 * q) + (b - 2 * q) + (c - 2 * q)) - 8.0 * q * q * q * (1.0 - kPi / 6.0);
    blended(box, all, q, false, rounded);
    FK_CHECK_NEAR(occtBox(a, b, c, all, q, false), rounded, 1e-6);
    FK_CHECK_NEAR(blended(box, all, q, true, 0.0), occtBox(a, b, c, all, q, true), 1e-6);
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

FK_TEST(BlendTangentChains) {
    // Rettangolo con gli angoli arrotondati: il bordo superiore e' una catena
    // di segmenti e archi tangenti (gli archi sono spigoli circolari aperti,
    // le zone finiscono nei piani normali agli spigoli nei vertici di tangenza).
    const double w = 10.0, d = 6.0, R = 1.5, h = 3.0, r = 0.5;
    const Operand slab = extrusion(roundedRectangle(Vec2(0, 0), w, d, R), h);
    const double waste = (1.0 - kPi / 4.0) * r * r, straight = 2.0 * (w - 2.0 * R) + 2.0 * (d - 2.0 * R);
    const double vSlab = (w * d - (4.0 - kPi) * R * R) * h;
    const std::vector<Vec3> top = edgesAtHeight(slab.body, h);
    FK_CHECK(top.size() == 8);
    blended(slab.body, top, r, false, vSlab - waste * straight - ringVolume(R, r, true));
    FK_CHECK_NEAR(occtBlended(slab.shape, top, r, false), vSlab - waste * straight - ringVolume(R, r, true), 1e-6);
    const double chamferRing = kTwoPi * (R - r / 3.0) * r * r / 2.0;
    blended(slab.body, top, r, true, vSlab - 0.5 * r * r * straight - chamferRing);
    // Un solo arco: finisce contro i fianchi piani tangenti.
    blended(slab.body, {Vec3(w - R + R * std::cos(0.25 * kPi), d - R + R * std::sin(0.25 * kPi), h)}, r, false, vSlab - 0.25 * ringVolume(R, r, true));

    // Tasca con gli angoli arrotondati in un blocco: il bordo (convesso, gli
    // archi su cilindri concavi) e il fondo (concavo: il raccordo aggiunge materiale).
    const double depth = 2.0;
    const Body block = makeBox(Frame3(), 14.0, 10.0, 5.0);
    const Body pocket = booleanOperation(block, makeExtrusion(Frame3(Vec3(2, 2, 5.0 - depth), Vec3(0, 0, 1), Vec3(1, 0, 0)),
                                                              buildProfile(roundedRectangle(Vec2(0, 0), w, d, R), 1e-9).regions.front(), depth + 1.0),
                                         BooleanOperation::Subtract);
    const double vPocket = 14.0 * 10.0 * 5.0 - (w * d - (4.0 - kPi) * R * R) * depth;
    std::vector<Vec3> rim, floor;
    for (const Vec3 &p : edgesAtHeight(pocket, 5.0))
        if (p.x() > 0.5 && p.x() < 13.5 && p.y() > 0.5 && p.y() < 9.5) rim.push_back(p);
    floor = edgesAtHeight(pocket, 5.0 - depth);
    FK_CHECK(rim.size() == 8 && floor.size() == 8);
    blended(pocket, rim, r, false, vPocket - waste * straight - ringVolume(R, r, false));
    blended(pocket, floor, r, false, vPocket + waste * straight + ringVolume(R, r, true));
}

FK_TEST(BlendOpenArcsAndObliqueEnds) {
    // Profilo a D: l'arco superiore finisce contro la faccia piana per l'asse.
    const double R = 3.0, h = 4.0, q = 0.5;
    const Operand dee = extrusion({arcSegment(Vec2(0, 0), R, -kHalfPi, kHalfPi), lineSegment(Vec2(0, R), Vec2(0, -R))}, h);
    const double vDee = 0.5 * kPi * R * R * h;
    blended(dee.body, {Vec3(R, 0, h)}, q, false, vDee - 0.5 * ringVolume(R, q, true));
    FK_CHECK_NEAR(occtBlended(dee.shape, {Vec3(R, 0, h)}, q, false), vDee - 0.5 * ringVolume(R, q, true), 1e-6);
    blended(dee.body, {Vec3(R, 0, h)}, q, true, vDee - 0.5 * kTwoPi * (R - q / 3.0) * q * q / 2.0);

    // Prisma trapezoidale: gli spigoli superiori finiscono contro fianchi obliqui.
    const Operand trapezoid = extrusion(polygon({Vec2(0, 0), Vec2(10, 0), Vec2(8, 5), Vec2(1, 5)}), 3.0);
    const double vTrapezoid = 0.5 * (10.0 + 7.0) * 5.0 * 3.0, r = 0.6;
    const std::vector<Vec3> bottomEdge{Vec3(5, 0, 3)};
    for (bool chamfer : {false, true}) {
        // Un solo spigolo: la zona tagliata dai fianchi obliqui.
        const double ours = blended(trapezoid.body, bottomEdge, r, chamfer, 0.0);
        FK_CHECK_NEAR(ours, occtBlended(trapezoid.shape, bottomEdge, r, chamfer), 1e-6);
        FK_CHECK(ours < vTrapezoid);
    }
    // Tutti i bordi della faccia superiore (angoli non retti) e un angolo con
    // tre spigoli tra piani non ortogonali (pezza sferica o triangolo).
    const std::vector<Vec3> top = edgesAtHeight(trapezoid.body, 3.0);
    FK_CHECK(top.size() == 4);
    const std::vector<Vec3> corner{Vec3(5, 0, 3), Vec3(9, 2.5, 3), Vec3(10, 0, 1.5)};
    for (bool chamfer : {false, true})
        FK_CHECK_NEAR(blended(trapezoid.body, top, r, chamfer, 0.0), occtBlended(trapezoid.shape, top, r, chamfer), 1e-6);
    FK_CHECK_NEAR(blended(trapezoid.body, corner, r, false, 0.0), occtBlended(trapezoid.shape, corner, r, false), 1e-6);
    // Smusso d'angolo tra piani non ortogonali: OCCT vi mette una pezza
    // B-spline di riempimento, il nuovo kernel il triangolo piano per i punti
    // in cui si incontrano i bordi degli smussi (come OCCT negli angoli retti).
    const double chamfered = blended(trapezoid.body, corner, r, true, 0.0);
    FK_CHECK(std::fabs(chamfered - occtBlended(trapezoid.shape, corner, r, true)) < 0.02);

    // Arco e segmento che si incontrano ad angolo (non tangenti): bordo superiore del profilo a D.
    const std::vector<Vec3> rim{Vec3(R, 0, h), Vec3(0, 0, h)};
    for (bool chamfer : {false, true})
        FK_CHECK_NEAR(blended(dee.body, rim, q, chamfer, 0.0), occtBlended(dee.shape, rim, q, chamfer), 1e-6);
}

namespace {

// Area e momento (rispetto allo spigolo, lungo la faccia piana) della zona
// tolta o aggiunta in sezione: raccordo di raggio r, smusso a distanza d.
void wasteSection(double size, bool chamfer, double &area, double &moment) {
    if (chamfer) {
        area = 0.5 * size * size;
        moment = size * size * size / 6.0;
    } else {
        area = (1.0 - kPi / 4.0) * size * size;
        moment = size * size * size * (5.0 / 6.0 - kPi / 4.0);
    }
}

// Volume spazzato dalla sezione lungo il bordo (Pappus-Guldino locale):
// A L - M * (rotazione della tangente verso la faccia piana).
double sweptVolume(double size, bool chamfer, double length, double turning) {
    double area, moment;
    wasteSection(size, chamfer, area, moment);
    return area * length - moment * turning;
}

ProfileSegment ellipseSegment(const Vec2 &center, double a, double b) {
    return {std::make_shared<Ellipse<2>>(makeEllipse(center, a, b)), {0.0, kTwoPi}};
}

double profileLength(const std::vector<ProfileSegment> &segments) {
    double length = 0.0;
    for (const ProfileSegment &s : segments) length += arcLength(*s.curve, s.range);
    return length;
}

}

FK_TEST(BlendFreeformClosedEdges) {
    // Cilindro ellittico: il bordo superiore e' un'ellisse (un solo edge chiuso).
    const double a = 3.0, b = 2.0, h = 4.0;
    const std::vector<ProfileSegment> ellipse{ellipseSegment(Vec2(0, 0), a, b)};
    const Operand cylinder = extrusion(ellipse, h);
    const double v = kPi * a * b * h, L = profileLength(ellipse);
    for (bool chamfer : {false, true}) {
        const double r = 0.4;
        const double expected = v - sweptVolume(r, chamfer, L, kTwoPi);
        const double ours = blended(cylinder.body, {Vec3(a, 0, h)}, r, chamfer, expected);
        const double occt = occtBlended(cylinder.shape, {Vec3(a, 0, h)}, r, chamfer);
        FK_CHECK(occt == 0.0 || std::fabs(ours - occt) < 1e-5 * v);
        // Entrambi i bordi (sopra e sotto).
        blended(cylinder.body, {Vec3(a, 0, h), Vec3(0, b, 0)}, r, chamfer, v - 2.0 * sweptVolume(r, chamfer, L, kTwoPi));
    }
    // Spline chiusa liscia (tangente continua anche nel punto di chiusura).
    const std::vector<Vec2> poles{Vec2(3, 0), Vec2(3, 2), Vec2(1, 3), Vec2(-2, 2.5), Vec2(-3, 0), Vec2(-2, -2.5), Vec2(1, -3), Vec2(3, -2), Vec2(3, 0)};
    const auto spline = std::make_shared<BSplineCurve<2>>(3, std::vector<double>{0, 0, 0, 0, 1. / 6, 2. / 6, 3. / 6, 4. / 6, 5. / 6, 1, 1, 1, 1}, poles);
    const std::vector<ProfileSegment> blob{{spline, spline->domain()}};
    const Operand smooth = extrusion(blob, 2.0);
    const double vBlob = area(buildProfile(blob, 1e-9).regions.front()) * 2.0, lBlob = profileLength(blob);
    const Vec3 onTop = embedCurve(std::make_shared<BSplineCurve<2>>(*spline), Frame3(Vec3(0, 0, 2), Vec3(0, 0, 1), Vec3(1, 0, 0)))->point(0.3);
    for (bool chamfer : {false, true}) {
        const double ours = blended(smooth.body, {onTop}, 0.3, chamfer, vBlob - sweptVolume(0.3, chamfer, lBlob, kTwoPi));
        // OCCT (tassellazione fine: BRepGProp sbaglia sui prismi da spline) a meno del rumore della mesh.
        if (!chamfer) {
            const double occt = occtRemoved(smooth.shape, {onTop}, 0.3, chamfer);
            FK_CHECK(std::isnan(occt) || std::fabs((vBlob - ours) - occt) < 5e-3 * (vBlob - ours));
        }
    }
    // Tasca ellittica in un blocco: il bordo (convesso, il foro gira dall'altra
    // parte) toglie A L + 2 pi M, il fondo (concavo) aggiunge A L - 2 pi M.
    const double depth = 2.0, r = 0.3;
    const Body block = booleanOperation(makeBox(Frame3(), 10, 8, 4),
                                        makeExtrusion(Frame3(Vec3(5, 4, 4.0 - depth), Vec3(0, 0, 1), Vec3(1, 0, 0)),
                                                      buildProfile({ellipseSegment(Vec2(0, 0), a, b)}, 1e-9).regions.front(), depth + 1.0),
                                        BooleanOperation::Subtract);
    const double vBlock = 320.0 - kPi * a * b * depth;
    blended(block, {Vec3(5 + a, 4, 4)}, r, false, vBlock - sweptVolume(r, false, L, -kTwoPi));
    blended(block, {Vec3(5 + a, 4, 4.0 - depth)}, r, false, vBlock + sweptVolume(r, false, L, kTwoPi));
}

FK_TEST(BlendFreeformOpenEdges) {
    // Profilo "a lapide": una spline sopra (tangenti orizzontali agli estremi)
    // tra due lati verticali. La spline da sola finisce contro i fianchi piani
    // normali a essa; la tangente non ruota in totale, quindi il volume tolto e' A L.
    const auto top = std::make_shared<BSplineCurve<2>>(
        3, std::vector<double>{0, 0, 0, 0, 0.5, 0.5, 0.5, 1, 1, 1, 1},
        std::vector<Vec2>{Vec2(6, 4), Vec2(5, 4), Vec2(4.5, 5.5), Vec2(3, 5.5), Vec2(1.5, 5.5), Vec2(1, 4), Vec2(0, 4)});
    const std::vector<ProfileSegment> profile{lineSegment(Vec2(0, 0), Vec2(6, 0)), lineSegment(Vec2(6, 0), Vec2(6, 4)), {top, top->domain()},
                                              lineSegment(Vec2(0, 4), Vec2(0, 0))};
    const double h = 3.0;
    const Operand stone = extrusion(profile, h);
    const double v = area(buildProfile(profile, 1e-9).regions.front()) * h, L = arcLength(*top, top->domain());
    const Vec3 onTop(3, 5.5, h);
    for (bool chamfer : {false, true}) blended(stone.body, {onTop}, 0.4, chamfer, v - sweptVolume(0.4, chamfer, L, 0.0));
    const double occt = occtRemoved(stone.shape, {onTop}, 0.4, false);
    FK_CHECK(std::isnan(occt) || std::fabs(sweptVolume(0.4, false, L, 0.0) - occt) < 5e-3 * occt);
    // Tutto il bordo superiore: angoli vivi tra la spline e i lati (a mitra).
    // OCCT qui non da' un risultato affidabile (il raccordo esce sbagliato):
    // gli angoli vivi si verificano sotto, sugli spigoli rettilinei, contro il
    // percorso delle booleane (a sua volta uguale a OCCT).
    const std::vector<Vec3> rim{onTop, Vec3(3, 0, h), Vec3(6, 2, h), Vec3(0, 2, h)};
    for (bool chamfer : {false, true}) {
        const double removed = v - blended(stone.body, rim, 0.4, chamfer, 0.0);
        FK_CHECK(removed > sweptVolume(0.4, chamfer, L + 6.0 + 8.0, 0.0) - 1.0 && removed < sweptVolume(0.4, chamfer, L + 6.0 + 8.0, 0.0));
    }
}

namespace {

// Le catene del nuovo percorso (blendPlanarChains) anche su segmenti e archi,
// contro il percorso con le booleane (blendEdges, verificato con OCCT).
void compareChains(const Body &body, const std::vector<Vec3> &points, double size) {
    std::vector<EdgeId> edges;
    for (const Vec3 &p : points) edges.push_back(nearestEdge(body, p, 1e-6));
    for (bool chamfer : {false, true}) {
        const double reference = blended(body, points, size, chamfer, 0.0);
        try {
            const Body chains = blendPlanarChains(body, edges, size, chamfer);
            for (const CheckIssue &issue : checkBody(chains)) reportFailure(__FILE__, __LINE__, describe(issue.code) + ": " + issue.message);
            FK_CHECK_NEAR(massProperties(chains).volume, reference, 1e-8 * reference);
            TessellationOptions options;
            options.deflection = 0.01;
            FK_CHECK(tessellate(chains, options).failedFaces == 0);
        } catch (const std::exception &error) {
            reportFailure(__FILE__, __LINE__, std::string(chamfer ? "catene smusso " : "catene raccordo ") + std::to_string(points.size()) + ": " + error.what());
        }
    }
}

}

FK_TEST(BlendChainsMatchBooleanPath) {
    // Faccia superiore di un parallelepipedo: quattro angoli vivi (a mitra).
    const Body box = makeBox(Frame3(), 10, 6, 4);
    compareChains(box, {Vec3(5, 0, 4), Vec3(10, 3, 4), Vec3(5, 6, 4), Vec3(0, 3, 4)}, 1.0);
    // Uno spigolo solo: la catena aperta finisce contro le facce normali.
    compareChains(box, {Vec3(5, 0, 4)}, 1.0);
    // Prisma trapezoidale: angoli vivi non retti.
    compareChains(extrusion(polygon({Vec2(0, 0), Vec2(10, 0), Vec2(8, 5), Vec2(1, 5)}), 3.0).body,
                  {Vec3(5, 0, 3), Vec3(9, 2.5, 3), Vec3(4.5, 5, 3), Vec3(0.5, 2.5, 3)}, 0.6);
    // Rettangolo arrotondato: catena liscia di segmenti e archi.
    const Body slab = extrusion(roundedRectangle(Vec2(0, 0), 10, 6, 1.5), 3.0).body;
    compareChains(slab, edgesAtHeight(slab, 3.0), 0.5);
}
