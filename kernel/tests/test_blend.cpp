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
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepLProp_SLProps.hxx>
#include <BRepTools.hxx>
#include <GProp_GProps.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>

#include "fk_blend.h"
#include "fk_blend_loop.h"
#include "fk_blend_surface.h"
#include "fk_surface_algo.h"
#include "fk_classify.h"
#include "fk_curve_algo.h"
#include "fk_curve_ops.h"
#include "fk_body_check.h"
#include "fk_boolean.h"
#include "fk_extrude.h"
#include "fk_mass.h"
#include "fk_loft.h"
#include "fk_primitives.h"
#include "fk_revolve.h"
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

LoftSection splitCircleAt(double z, double radius) {
    LoftSection section;
    section.frame = Frame3(Vec3(0, 0, z), Vec3(0, 0, 1), Vec3(1, 0, 0));
    for (int quarter = 0; quarter < 4; ++quarter)
        section.loop.segments.push_back(arcSegment(Vec2(0, 0), radius, quarter * kHalfPi, (quarter + 1) * kHalfPi));
    return section;
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
    // Un solo arco: il raccordo prosegue per tangenza su tutto il bordo (come nei CAD e in OCCT).
    blended(slab.body, {Vec3(w - R + R * std::cos(0.25 * kPi), d - R + R * std::sin(0.25 * kPi), h)}, r, false, vSlab - waste * straight - ringVolume(R, r, true));

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

namespace {

// Volume tolto (o aggiunto, negativo) da OCCT secondo BRepGProp (niente
// prismi da spline qui), NaN se OCCT fallisce.
double occtRemovedExact(const TopoDS_Shape &shape, const std::vector<Vec3> &points, double size) {
    const TopoDS_Shape result = occtBlendedShape(shape, points, size, false);
    if (result.IsNull()) return std::nan("");
    GProp_GProps before, after;
    BRepGProp::VolumeProperties(shape, before, 1e-12);
    BRepGProp::VolumeProperties(result, after, 1e-12);
    return before.Mass() - after.Mass();
}

// Raccordo generale (fk_blend_surface) degli spigoli per i punti: body valido,
// continuita' G1 lungo le curve di contatto (raccordi), volume atteso entro
// `relative` (se expected > 0), tassellazione riuscita. Restituisce il volume.
double surfaceBlended(const Body &body, const std::vector<Vec3> &points, double size, bool chamfer, double expected, double relative = 1e-9) {
    std::vector<EdgeId> edges;
    for (const Vec3 &p : points) {
        const EdgeId e = nearestEdge(body, p, 1e-6);
        FK_CHECK(e.valid());
        if (!e.valid()) return 0.0;
        edges.push_back(e);
    }
    Body result;
    try {
        result = blendSurfaceChains(body, surfaceChainRuns(body, edges, edges), size, chamfer);
    } catch (const std::exception &error) {
        reportFailure(__FILE__, __LINE__, std::string(chamfer ? "smusso generale " : "raccordo generale ") + std::to_string(points.front().x()) + " "
                                              + std::to_string(points.front().y()) + " " + std::to_string(points.front().z()) + ": " + error.what());
        return 0.0;
    }
    for (const CheckIssue &issue : checkBody(result)) reportFailure(__FILE__, __LINE__, describe(issue.code) + ": " + issue.message);
    // Facce nuove: superfici che il body di partenza non aveva.
    std::vector<const Surface *> old;
    for (FaceId f : body.faces()) old.push_back(body.face(f).surface.get());
    auto isNew = [&](FaceId f) { return std::find(old.begin(), old.end(), result.face(f).surface.get()) == old.end(); };
    auto outward = [&](FinId fin, double t) {
        const Face &face = result.face(result.finFace(fin));
        const Vec2 uv = result.fin(fin).pcurve->point(t);
        const Vec3 n = face.surface->normal(uv.x(), uv.y());
        return face.sense ? n : -n;
    };
    double worst = 0.0;
    int contacts = 0;
    for (EdgeId e : result.edges()) {
        const Edge &edge = result.edge(e);
        const bool a = isNew(result.finFace(edge.forward)), b = isNew(result.finFace(edge.backward));
        if (a == b || chamfer) continue;
        // Solo le curve di contatto (v = 0 o 1 sulla faccia nuova), non gli archi d'estremita'.
        const FinId onNew = a ? edge.forward : edge.backward;
        const double v = result.fin(onNew).pcurve->point(0.5 * (edge.range.lo + edge.range.hi)).y();
        if (std::fabs(v) > 1e-6 && std::fabs(v - 1.0) > 1e-6) continue;
        ++contacts;
        for (double f : {0.1, 0.37, 0.5, 0.81}) {
            const double t = edge.range.lo + f * edge.range.length();
            worst = std::max(worst, norm(outward(edge.forward, t) - outward(edge.backward, t)));
        }
    }
    if (!chamfer) FK_CHECK(contacts > 0);
    FK_CHECK(worst < 1e-6);
    // Palla rotolante: da ogni punto P del raccordo il centro c = P -+ r n dista
    // esattamente r da entrambe le facce dello spigolo (le superfici vecchie).
    if (!chamfer) {
        std::vector<const Surface *> sides;
        for (EdgeId e : edges)
            for (FinId fin : {body.edge(e).forward, body.edge(e).backward}) sides.push_back(body.face(body.finFace(fin)).surface.get());
        double ball = 0.0;
        for (FaceId f : result.faces()) {
            if (!isNew(f)) continue;
            const Surface &surface = *result.face(f).surface;
            const Interval u = surface.uDomain();
            for (double fu : {0.1, 0.3, 0.5, 0.7, 0.9})
                for (double fv : {0.0, 0.25, 0.5, 0.75, 1.0}) {
                    const double uu = u.lo + fu * u.length();
                    const Vec3 p = surface.point(uu, fv), n = surface.normal(uu, fv);
                    double best = 1e300;
                    for (double sign : {1.0, -1.0}) {
                        const Vec3 c = p + sign * size * n;
                        double gap = 0.0;
                        int touching = 0;
                        for (const Surface *side : sides) {
                            const double d = projectPoint(*side, c).distance;
                            if (std::fabs(d - size) < 1e-3 * size) ++touching;
                            gap = std::max(gap, std::fabs(d - size) < 1e-3 * size ? std::fabs(d - size) : 0.0);
                        }
                        if (touching >= 2) best = std::min(best, gap);
                    }
                    ball = std::max(ball, best);
                }
        }
        FK_CHECK(ball < 1e-7 * size);
    }
    const double volume = massProperties(result).volume;
    if (expected > 0.0) FK_CHECK_NEAR(volume, expected, relative * expected);
    TessellationOptions options;
    options.deflection = 0.01;
    FK_CHECK(tessellate(result, options).failedFaces == 0);
    return volume;
}

// Punti medi degli edge che soddisfano il predicato.
template <class Predicate>
std::vector<Vec3> edgeMidpoints(const Body &body, Predicate predicate) {
    std::vector<Vec3> points;
    for (EdgeId e : body.edges()) {
        const Edge &edge = body.edge(e);
        const Vec3 p = edge.curve->point(0.5 * (edge.range.lo + edge.range.hi));
        if (predicate(edge, p)) points.push_back(p);
    }
    return points;
}

}

FK_TEST(BlendSurfaceMatchesExactPlanarCases) {
    // Il raccordo generale sui casi con il volume esatto (sezione che scorre
    // lungo una curva piana): ellisse, spline chiusa, tasca ellittica (bordo
    // convesso e fondo concavo), spline aperta contro i fianchi normali.
    const double a = 3.0, b = 2.0, h = 4.0;
    const std::vector<ProfileSegment> ellipse{ellipseSegment(Vec2(0, 0), a, b)};
    const Operand cylinder = extrusion(ellipse, h);
    const double v = kPi * a * b * h, L = profileLength(ellipse);
    for (bool chamfer : {false, true}) {
        surfaceBlended(cylinder.body, {Vec3(a, 0, h)}, 0.4, chamfer, v - sweptVolume(0.4, chamfer, L, kTwoPi));
        surfaceBlended(cylinder.body, {Vec3(a, 0, h), Vec3(0, b, 0)}, 0.4, chamfer, v - 2.0 * sweptVolume(0.4, chamfer, L, kTwoPi));
    }
    const std::vector<Vec2> poles{Vec2(3, 0), Vec2(3, 2), Vec2(1, 3), Vec2(-2, 2.5), Vec2(-3, 0), Vec2(-2, -2.5), Vec2(1, -3), Vec2(3, -2), Vec2(3, 0)};
    const auto spline = std::make_shared<BSplineCurve<2>>(3, std::vector<double>{0, 0, 0, 0, 1. / 6, 2. / 6, 3. / 6, 4. / 6, 5. / 6, 1, 1, 1, 1}, poles);
    const std::vector<ProfileSegment> blob{{spline, spline->domain()}};
    const Operand smooth = extrusion(blob, 2.0);
    const double vBlob = area(buildProfile(blob, 1e-9).regions.front()) * 2.0, lBlob = profileLength(blob);
    const Vec3 onTop = embedCurve(std::make_shared<BSplineCurve<2>>(*spline), Frame3(Vec3(0, 0, 2), Vec3(0, 0, 1), Vec3(1, 0, 0)))->point(0.3);
    for (bool chamfer : {false, true}) surfaceBlended(smooth.body, {onTop}, 0.3, chamfer, vBlob - sweptVolume(0.3, chamfer, lBlob, kTwoPi));
    const double depth = 2.0, r = 0.3;
    const Body block = booleanOperation(makeBox(Frame3(), 10, 8, 4),
                                        makeExtrusion(Frame3(Vec3(5, 4, 4.0 - depth), Vec3(0, 0, 1), Vec3(1, 0, 0)),
                                                      buildProfile({ellipseSegment(Vec2(0, 0), a, b)}, 1e-9).regions.front(), depth + 1.0),
                                        BooleanOperation::Subtract);
    const double vBlock = 320.0 - kPi * a * b * depth;
    surfaceBlended(block, {Vec3(5 + a, 4, 4)}, r, false, vBlock - sweptVolume(r, false, L, -kTwoPi));
    surfaceBlended(block, {Vec3(5 + a, 4, 4.0 - depth)}, r, false, vBlock + sweptVolume(r, false, L, kTwoPi));
    surfaceBlended(block, {Vec3(5 + a, 4, 4.0 - depth)}, r, true, vBlock + sweptVolume(r, true, L, kTwoPi));
    // Spline aperta tra due fianchi piani normali ai suoi estremi.
    const auto top = std::make_shared<BSplineCurve<2>>(
        3, std::vector<double>{0, 0, 0, 0, 0.5, 0.5, 0.5, 1, 1, 1, 1},
        std::vector<Vec2>{Vec2(6, 4), Vec2(5, 4), Vec2(4.5, 5.5), Vec2(3, 5.5), Vec2(1.5, 5.5), Vec2(1, 4), Vec2(0, 4)});
    const std::vector<ProfileSegment> profile{lineSegment(Vec2(0, 0), Vec2(6, 0)), lineSegment(Vec2(6, 0), Vec2(6, 4)), {top, top->domain()},
                                              lineSegment(Vec2(0, 4), Vec2(0, 0))};
    const Operand stone = extrusion(profile, 3.0);
    const double vStone = area(buildProfile(profile, 1e-9).regions.front()) * 3.0, lTop = arcLength(*top, top->domain());
    for (bool chamfer : {false, true}) surfaceBlended(stone.body, {Vec3(3, 5.5, 3.0)}, 0.4, chamfer, vStone - sweptVolume(0.4, chamfer, lTop, 0.0));
}

FK_TEST(BlendSurfaceMatchesCircularCases) {
    // Cerchi tra superfici coassiali (Pappus): bordo superiore di un cilindro,
    // bordo di un foro, base di un perno (concavo).
    const double R = 3.0, H = 4.0, q = 0.5;
    const Body cylinder = makeCylinder(Frame3(Vec3(1, 2, 3), Vec3(0, 0, 1), Vec3(1, 0, 0)), R, H);
    const double vCyl = kPi * R * R * H;
    surfaceBlended(cylinder, {Vec3(1 + R, 2, 3 + H)}, q, false, vCyl - ringVolume(R, q, true));
    const Body block = booleanOperation(makeBox(Frame3(), 10, 10, 4), makeCylinder(Frame3(Vec3(5, 5, -1), Vec3(0, 0, 1), Vec3(1, 0, 0)), 2.0, 6.0),
                                        BooleanOperation::Subtract);
    surfaceBlended(block, {Vec3(7, 5, 4)}, q, false, 400.0 - kPi * 4.0 * 4.0 - ringVolume(2.0, q, false));
    const Body boss = booleanOperation(makeBox(Frame3(), 10, 10, 4), makeCylinder(Frame3(Vec3(5, 5, 4), Vec3(0, 0, 1), Vec3(1, 0, 0)), 2.0, 3.0),
                                       BooleanOperation::Unite);
    // Il raccordo concavo aggiunge l'anello attorno al perno: quadrato r x r meno il quarto di cerchio, fuori dal raggio 2.
    const double added = kTwoPi * (q * q * (2.0 + q / 2.0) - kPi * q * q / 4.0 * (2.0 + q - 4.0 * q / (3.0 * kPi)));
    surfaceBlended(boss, {Vec3(7, 5, 4)}, q, false, 400.0 + kPi * 4.0 * 3.0 + added);
    // Lo stesso con il percorso dell'app (blendEdges sceglie da solo il modulo analitico).
    FK_CHECK_NEAR(blended(boss, {Vec3(7, 5, 4)}, q, false, 0.0), 400.0 + kPi * 4.0 * 3.0 + added, 1e-9 * 450.0);
}

FK_TEST(BlendSurfaceNonAnalyticEdges) {
    // Cilindro obliquo su un blocco: lo spigolo alla base e' un'ellisse tra il
    // piano e un cilindro non coassiale (concavo), quello in cima un'ellisse
    // convessa (il cilindro e' tagliato da un piano orizzontale).
    const Vec3 axis = normalized(Vec3(0.5, 0.0, 1.0));
    const Frame3 tilted(Vec3(5, 5, 0), axis, Vec3(0, 1, 0));
    const Body box = makeBox(Frame3(), 10, 10, 4), slab = makeBox(Frame3(Vec3(-5, -5, 6), Vec3(0, 0, 1), Vec3(1, 0, 0)), 20, 20, 10);
    const Body post = booleanOperation(makeCylinder(tilted, 1.5, 8.0), slab, BooleanOperation::Subtract);
    const Body oblique = booleanOperation(box, post, BooleanOperation::Unite);
    gp_Ax2 occtAxis(gp_Pnt(5, 5, 0), gp_Dir(axis.x(), axis.y(), axis.z()), gp_Dir(0, 1, 0));
    const TopoDS_Shape occtPost = BRepAlgoAPI_Cut(BRepPrimAPI_MakeCylinder(occtAxis, 1.5, 8.0).Shape(),
                                                  BRepPrimAPI_MakeBox(gp_Pnt(-5, -5, 6), 20, 20, 10).Shape()).Shape();
    const TopoDS_Shape occtOblique = BRepAlgoAPI_Fuse(BRepPrimAPI_MakeBox(10, 10, 4).Shape(), occtPost).Shape();
    const std::vector<Vec3> base = edgeMidpoints(oblique, [](const Edge &e, const Vec3 &p) { return e.curve->type() == CurveType::Ellipse && std::fabs(p.z() - 4) < 1e-9; });
    const std::vector<Vec3> cap = edgeMidpoints(oblique, [](const Edge &e, const Vec3 &p) { return e.curve->type() == CurveType::Ellipse && std::fabs(p.z() - 6) < 1e-9; });
    FK_CHECK(base.size() == 1 && cap.size() == 1);
    if (base.size() != 1 || cap.size() != 1) return;
    const double vOblique = massProperties(oblique).volume;
    for (const auto &[points, r] : {std::pair<std::vector<Vec3>, double>{base, 0.4}, {cap, 0.3}, {{base.front(), cap.front()}, 0.3}}) {
        // Dal percorso dell'app (blendEdges) e dal modulo generale direttamente: stesso risultato.
        const double ours = blended(oblique, points, r, false, 0.0);
        FK_CHECK_NEAR(surfaceBlended(oblique, points, r, false, 0.0), ours, 1e-9 * vOblique);
        const double occt = occtRemovedExact(occtOblique, points, r);
        FK_CHECK(std::isnan(occt) || std::fabs((vOblique - ours) - occt) < 1e-2 * std::fabs(occt));
        surfaceBlended(oblique, points, r, true, 0.0);
    }

    // Innesto a T tra due cilindri (curva del marching, concava) e foro
    // trasversale in un cilindro (due curve convesse).
    const Body main = makeCylinder(Frame3(Vec3(-5, 0, 0), Vec3(1, 0, 0), Vec3(0, 1, 0)), 2.0, 10.0);
    const Body tee = booleanOperation(main, makeCylinder(Frame3(Vec3(0, 0, 0), Vec3(0, 0, 1), Vec3(1, 0, 0)), 1.0, 4.0), BooleanOperation::Unite);
    const TopoDS_Shape occtMain = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(-5, 0, 0), gp_Dir(1, 0, 0), gp_Dir(0, 1, 0)), 2.0, 10.0).Shape();
    const TopoDS_Shape occtTee = BRepAlgoAPI_Fuse(occtMain, BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1), gp_Dir(1, 0, 0)), 1.0, 4.0).Shape()).Shape();
    const std::vector<Vec3> junction = edgeMidpoints(tee, [](const Edge &e, const Vec3 &p) { return e.curve->type() == CurveType::BSpline && p.z() > 0.5; });
    FK_CHECK(!junction.empty());
    const double vTee = massProperties(tee).volume;
    if (!junction.empty()) {
        const double ours = blended(tee, {junction.front()}, 0.3, false, 0.0);
        const double occt = occtRemovedExact(occtTee, {junction.front()}, 0.3);
        FK_CHECK(ours > vTee);
        FK_CHECK(std::isnan(occt) || std::fabs((vTee - ours) - occt) < 1e-2 * std::fabs(occt));
        FK_CHECK_NEAR(surfaceBlended(tee, {junction.front()}, 0.3, false, 0.0), ours, 1e-9 * vTee);
        surfaceBlended(tee, {junction.front()}, 0.3, true, 0.0);
    }
    const Body drilled = booleanOperation(main, makeCylinder(Frame3(Vec3(0, 0, -3), Vec3(0, 0, 1), Vec3(1, 0, 0)), 0.8, 6.0), BooleanOperation::Subtract);
    const TopoDS_Shape occtDrilled = BRepAlgoAPI_Cut(occtMain, BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, -3), gp_Dir(0, 0, 1), gp_Dir(1, 0, 0)), 0.8, 6.0).Shape()).Shape();
    const std::vector<Vec3> mouths = edgeMidpoints(drilled, [](const Edge &e, const Vec3 &) { return e.curve->type() == CurveType::BSpline; });
    FK_CHECK(mouths.size() >= 2);
    const double vDrilled = massProperties(drilled).volume;
    if (!mouths.empty()) {
        const std::vector<Vec3> upper = edgeMidpoints(drilled, [](const Edge &e, const Vec3 &p) { return e.curve->type() == CurveType::BSpline && p.z() > 0.0; });
        const double ours = blended(drilled, upper, 0.25, false, 0.0);
        const double occt = occtRemovedExact(occtDrilled, upper, 0.25);
        FK_CHECK(ours < vDrilled);
        FK_CHECK(std::isnan(occt) || std::fabs((vDrilled - ours) - occt) < 1e-2 * std::fabs(occt));
        FK_CHECK_NEAR(surfaceBlended(drilled, mouths, 0.25, false, 0.0), vDrilled - 2.0 * (vDrilled - ours), 1e-9 * vDrilled);
        blended(drilled, mouths, 0.25, true, 0.0);
    }
}

FK_TEST(BlendSurfaceTeeIsExactRollingBall) {
    // Innesto a T tra un cilindro di raggio 2 lungo x e uno di raggio 1 lungo z:
    // la palla di raggio r che rotola nell'angolo (concavo) ha il centro a
    // 2 + r dall'asse x e a 1 + r dall'asse z. Nei piani di simmetria le
    // sezioni sono note in forma chiusa.
    const Body main = makeCylinder(Frame3(Vec3(-5, 0, 0), Vec3(1, 0, 0), Vec3(0, 1, 0)), 2.0, 10.0);
    const Body tee = booleanOperation(main, makeCylinder(Frame3(Vec3(0, 0, 0), Vec3(0, 0, 1), Vec3(1, 0, 0)), 1.0, 4.0), BooleanOperation::Unite);
    const std::vector<Vec3> junction = edgeMidpoints(tee, [](const Edge &e, const Vec3 &p) { return e.curve->type() == CurveType::BSpline && p.z() > 0.5; });
    FK_CHECK(junction.size() == 1);
    if (junction.empty()) return;
    const double r = 0.3;
    const Body result = blendEdges(tee, {nearestEdge(tee, junction.front(), 1e-6)}, r, false);
    FK_CHECK(checkBody(result).empty());
    auto ballError = [r](const Vec3 &p, const Vec3 &n) {
        double best = 1e300;
        for (double sign : {1.0, -1.0}) {
            const Vec3 c = p + sign * r * n;
            best = std::min(best, std::max(std::fabs(std::hypot(c.y(), c.z()) - (2 + r)), std::fabs(std::hypot(c.x(), c.y()) - (1 + r))));
        }
        return best;
    };
    double worst = 0.0;
    int blends = 0;
    for (FaceId f : result.faces()) {
        if (result.face(f).surface->type() != SurfaceType::BSpline) continue;
        ++blends;
        const Surface &surface = *result.face(f).surface;
        const Interval u = surface.uDomain();
        for (double fu = 0.05; fu < 1.0; fu += 0.1)
            for (double fv = 0.0; fv <= 1.0; fv += 0.125)
                worst = std::max(worst, ballError(surface.point(u.lo + fu * u.length(), fv), surface.normal(u.lo + fu * u.length(), fv)));
    }
    FK_CHECK(blends == 2);
    FK_CHECK(worst < 1e-8);
    // Punti esatti: nel piano x = 0 centro (0, 1 + r, zc), nel piano y = 0 centro (1 + r, 0, 2 + r).
    const double zc = std::sqrt((2 + r) * (2 + r) - (1 + r) * (1 + r));
    for (const Vec3 &p : {Vec3(0, 1, zc), Vec3(0, (1 + r) * 2 / (2 + r), zc * 2 / (2 + r)), Vec3(1, 0, 2 + r), Vec3(1 + r, 0, 2)}) {
        double best = 1e300;
        for (FaceId f : result.faces()) {
            const SurfaceProjection projection = projectPoint(*result.face(f).surface, p);
            if (classifyPointOnFace(result, f, projection.point, 1e-7) != PointLocation::Outside) best = std::min(best, projection.distance);
        }
        FK_CHECK(best < 1e-9);
    }
    // OCCT (BRepFilletAPI) approssima: la sua faccia si scosta dalla palla esatta di qualche 1e-4.
    const TopoDS_Shape occtMain = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(-5, 0, 0), gp_Dir(1, 0, 0), gp_Dir(0, 1, 0)), 2.0, 10.0).Shape();
    const TopoDS_Shape occtTee = BRepAlgoAPI_Fuse(occtMain, BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1), gp_Dir(1, 0, 0)), 1.0, 4.0).Shape()).Shape();
    const TopoDS_Shape occtResult = occtBlendedShape(occtTee, {junction.front()}, r, false);
    double occtWorst = 0.0;
    for (TopExp_Explorer faces(occtResult, TopAbs_FACE); faces.More(); faces.Next()) {
        const TopoDS_Face &face = TopoDS::Face(faces.Current());
        BRepAdaptor_Surface adaptor(face);
        if (adaptor.GetType() == GeomAbs_Cylinder || adaptor.GetType() == GeomAbs_Plane) continue;
        double u0, u1, v0, v1;
        BRepTools::UVBounds(face, u0, u1, v0, v1);
        for (double fu = 0.05; fu < 1.0; fu += 0.1)
            for (double fv = 0.05; fv < 1.0; fv += 0.1) {
                BRepLProp_SLProps props(adaptor, u0 + fu * (u1 - u0), v0 + fv * (v1 - v0), 1, 1e-9);
                if (props.IsNormalDefined()) occtWorst = std::max(occtWorst, ballError(fromOcct(props.Value()), fromOcct(gp_Vec(props.Normal().XYZ()))));
            }
    }
    FK_CHECK(occtResult.IsNull() || occtWorst > 1e3 * worst);
}

namespace {

// Volume OCCT del parallelepipedo con gli spigoli per i punti smussati di d1 (sulla faccia la
// cui normale e' piu' vicina a `reference`) e d2.
double occtAsymmetricBox(double a, double b, double c, const std::vector<Vec3> &points, const Vec3 &reference, double d1, double d2) {
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(a, b, c).Shape();
    TopTools_IndexedDataMapOfShapeListOfShape edgeFaces;
    TopExp::MapShapesAndAncestors(box, TopAbs_EDGE, TopAbs_FACE, edgeFaces);
    BRepFilletAPI_MakeChamfer maker(box);
    for (const Vec3 &p : points) {
        const TopoDS_Shape vertex = BRepBuilderAPI_MakeVertex(gp_Pnt(p.x(), p.y(), p.z())).Shape();
        int best = 1;
        double closest = 1e300;
        for (int i = 1; i <= edgeFaces.Extent(); ++i) {
            BRepExtrema_DistShapeShape distance(vertex, edgeFaces.FindKey(i));
            if (distance.Value() < closest) closest = distance.Value(), best = i;
        }
        TopoDS_Face chosen;
        double score = -1e300;
        for (const TopoDS_Shape &face : edgeFaces(best)) {
            BRepAdaptor_Surface surface(TopoDS::Face(face));
            gp_Dir n = surface.Plane().Axis().Direction();
            if (face.Orientation() == TopAbs_REVERSED) n.Reverse();
            const double d = n.X() * reference.x() + n.Y() * reference.y() + n.Z() * reference.z();
            if (d > score) score = d, chosen = TopoDS::Face(face);
        }
        maker.Add(d1, d2, TopoDS::Edge(edgeFaces.FindKey(best)), chosen);
    }
    GProp_GProps props;
    BRepGProp::VolumeProperties(maker.Shape(), props, 1e-12);
    return props.Mass();
}

double chamferedVolume(const Body &body, const std::vector<Vec3> &points, const ChamferSides &sides) {
    std::vector<EdgeId> edges;
    for (const Vec3 &p : points) edges.push_back(nearestEdge(body, p, 1e-6));
    Body result;
    try {
        result = chamferEdges(body, edges, std::vector<ChamferSides>(edges.size(), sides));
    } catch (const std::exception &error) {
        reportFailure(__FILE__, __LINE__, std::string("smusso asimmetrico: ") + error.what());
        return 0.0;
    }
    FK_CHECK(checkBody(result).empty());
    return massProperties(result).volume;
}

}

FK_TEST(ChamferTwoDistances) {
    const double a = 10, b = 6, c = 4;
    const Body box = makeBox(Frame3(), a, b, c);
    const Vec3 up(0, 0, 1);
    for (const auto &[d1, d2] : {std::pair<double, double>{1.0, 0.4}, {0.4, 1.0}}) {
        // Spigolo in alto lungo x: d1 sulla faccia in alto, d2 su quella davanti.
        const double v = chamferedVolume(box, {Vec3(5, 0, 4)}, {up, d1, d2});
        FK_CHECK_NEAR(v, a * b * c - 0.5 * d1 * d2 * a, 1e-9 * a * b * c);
        FK_CHECK_NEAR(v, occtAsymmetricBox(a, b, c, {Vec3(5, 0, 4)}, up, d1, d2), 1e-7);
        // Il vertice nuovo sulla faccia in alto sta a d1 dallo spigolo.
        Body result = chamferEdges(box, {nearestEdge(box, Vec3(5, 0, 4), 1e-6)}, {{up, d1, d2}});
        bool found = false;
        for (VertexId vertex : result.vertices()) found = found || distance(result.vertex(vertex).point, Vec3(0, d1, 4)) < 1e-9;
        FK_CHECK(found);
        // Tre spigoli in un vertice (pezza d'angolo) e i dodici: come OCCT.
        const std::vector<Vec3> three{Vec3(5, 0, 4), Vec3(10, 3, 4), Vec3(10, 0, 2)};
        FK_CHECK_NEAR(chamferedVolume(box, three, {up, d1, d2}), occtAsymmetricBox(a, b, c, three, up, d1, d2), 1e-7);
    }
    // Bordi circolari (Pappus: triangolo di lati dT in alto e dW sul fianco, baricentro a R - dT / 3).
    const double R = 3.0, H = 4.0, dT = 0.6, dW = 0.25;
    const Body cylinder = makeCylinder(Frame3(), R, H);
    FK_CHECK_NEAR(chamferedVolume(cylinder, {Vec3(R, 0, H)}, {up, dT, dW}), kPi * R * R * H - kTwoPi * (R - dT / 3.0) * 0.5 * dT * dW, 1e-9 * 100);
    // Bordo di forma libera (catena piana: ellisse) e lo stesso con il modulo generale.
    const double ea = 3.0, eb = 2.0;
    const std::vector<ProfileSegment> ellipse{ellipseSegment(Vec2(0, 0), ea, eb)};
    const Operand elliptic = extrusion(ellipse, H);
    const double L = profileLength(ellipse), area = 0.5 * dT * dW, moment = area * dT / 3.0;
    const double expected = kPi * ea * eb * H - (area * L - moment * kTwoPi);
    FK_CHECK_NEAR(chamferedVolume(elliptic.body, {Vec3(ea, 0, H)}, {up, dT, dW}), expected, 1e-9 * expected);
    const EdgeId rim = nearestEdge(elliptic.body, Vec3(ea, 0, H), 1e-6);
    const std::vector<ChamferSides> rimSides{{up, dT, dW}};
    const Body general = blendSurfaceChains(elliptic.body, {rim}, std::max(dT, dW), true, &rimSides);
    FK_CHECK(checkBody(general).empty());
    FK_CHECK_NEAR(massProperties(general).volume, expected, 1e-9 * expected);
}

FK_TEST(BlendSmallRadiusOnLargeRims) {
    // Raggi piccoli rispetto al bordo: la corona dell'utensile attorno al cerchio
    // e' sottile e i poligoni (u, v) delle booleane devono infittirsi.
    for (double r : {0.1, 0.5, 2.0}) {
        const Body cylinder = makeCylinder(Frame3(), 12.1, 110.0);
        blended(cylinder, {Vec3(12.1, 0, 110)}, r, false, kPi * 12.1 * 12.1 * 110.0 - ringVolume(12.1, r, true));
    }
    // Il flacone (rivoluzione con fondo sferico, spalla e collo): il bordo del collo e quello del fondo.
    const auto arc = [](const Vec2 &c, const Vec2 &a, const Vec2 &b) {
        const double t0 = std::atan2(a.y() - c.y(), a.x() - c.x()), t1 = std::atan2(b.y() - c.y(), b.x() - c.x());
        return arcSegment(c, distance(c, a), std::min(t0, t1), std::max(t0, t1));
    };
    const std::vector<ProfileSegment> profile{lineSegment(Vec2(0, 110), Vec2(12.1, 110)), lineSegment(Vec2(12.1, 110), Vec2(12.1, 95.8)),
                                              lineSegment(Vec2(12.1, 95.8), Vec2(14, 95.8)), arc(Vec2(14, 85.8), Vec2(24, 85.8), Vec2(14, 95.8)),
                                              lineSegment(Vec2(24, 85.8), Vec2(24, 0)), lineSegment(Vec2(24, 0), Vec2(21, 0)),
                                              arc(Vec2(0, -220), Vec2(21, 0), Vec2(0, 1)), lineSegment(Vec2(0, 1), Vec2(0, 110))};
    const Body bottle = makeRevolution(Frame3(), buildProfile(profile, 1e-9).regions.front());
    const double v = massProperties(bottle).volume;
    blended(bottle, {Vec3(12.1, 0, 110)}, 0.5, false, v - ringVolume(12.1, 0.5, true));
    blended(bottle, {Vec3(24, 0, 0)}, 0.5, false, v - ringVolume(24.0, 0.5, true));
    // Una selezione con uno spigolo liscio (corpo cilindrico e spalla toroidale, tangenti): si lascia.
    FK_CHECK_NEAR(blended(bottle, {Vec3(12.1, 0, 110), Vec3(24, 0, 85.8)}, 0.5, false, 0.0), v - ringVolume(12.1, 0.5, true), 1e-9 * v);
    FK_CHECK_THROWS(blendEdges(bottle, {nearestEdge(bottle, Vec3(24, 0, 85.8), 1e-6)}, 0.5, false));
}

FK_TEST(BlendLoftCircularCapsSplitIntoPatches) {
    // Una loft liscia tra cerchi suddivisi in quarti produce quattro fianchi
    // B-spline. I bordi dei coperchi sono quindi catene circolari con giunti
    // tra patch, come nel modello prova con loft-CerchiCerchio.prt.
    const double endRadius = 7.4, middleRadius = 15.0, height = 100.0;
    const Body loft = loftSolid({splitCircleAt(0.0, endRadius), splitCircleAt(0.5 * height, middleRadius), splitCircleAt(height, endRadius)}, false);
    const double c = std::sqrt(0.5) * endRadius;
    for (double radius : {0.01, 0.1}) {
        blended(loft, {Vec3(c, c, 0.0)}, radius, false, 0.0);
        blended(loft, {Vec3(c, c, height)}, radius, false, 0.0);
    }
}

FK_TEST(BlendEdgeEndingOnExistingFilletCornerPatch) {
    Body body = makeBox(Frame3(), 10.0, 8.0, 20.0);
    body = blendEdges(body, {nearestEdge(body, Vec3(5, 0, 20), 1e-6)}, 1.0, false);
    FK_CHECK(checkBody(body).empty());
    const EdgeId longitudinal = nearestEdge(body, Vec3(10.0, 0.0, 10.0), 1e-6);
    FK_CHECK(longitudinal.valid());
    if (longitudinal.valid()) {
        body = blendEdges(body, {longitudinal}, 0.5, false);
        FK_CHECK(checkBody(body).empty());
        TessellationOptions options; options.deflection = 0.02;
        FK_CHECK(tessellate(body, options).failedFaces == 0);
        FK_CHECK(massProperties(body).volume > 0.0);
    }
}

FK_TEST(BlendPlanarChainConcaveCornersBetweenLongArcs) {
    // Contorno superiore dell'unione di un disco (R = 16) e di due lobi
    // (R = 6, centri a +-d): archi lunghi (i lobi quasi 270 gradi) che si
    // incontrano in angoli vivi concavi, come il profilo di "prova con
    // loft-CerchiCerchio.prt" prima dei raccordi degli angoli. Con r = 1 le
    // parallele dei due archi sono quasi tangenti (16 + 6 - 2 r appena sopra
    // d): Gauss-Newton dal vertice divergeva e l'allungamento dei lobi dai due
    // lati superava il giro completo.
    const double h = 5.0, d = 19.8, R = 16.0, a = 6.0, r = 1.0;
    Body body = makeCylinder(Frame3(Vec3(0, 0, 0), Vec3(0, 0, 1), Vec3(1, 0, 0)), R, h);
    for (double x : {-d, d})
        body = booleanOperation(body, makeCylinder(Frame3(Vec3(x, 0, 0), Vec3(0, 0, 1), Vec3(1, 0, 0)), a, h), BooleanOperation::Unite);
    FaceId top;
    for (FaceId f : body.faces()) {
        const auto *plane = dynamic_cast<const Plane *>(body.face(f).surface.get());
        if (plane && std::fabs(plane->frame().origin().z() - h) < 1e-9 && std::fabs(std::fabs(plane->frame().zDir().z()) - 1.0) < 1e-12) top = f;
    }
    FK_CHECK(top.valid());
    if (!top.valid()) return;
    std::vector<EdgeId> edges;
    for (LoopId loop : body.face(top).loops)
        for (FinId fin : body.loopFins(loop)) edges.push_back(body.fin(fin).edge);
    FK_CHECK(edges.size() == 4);
    try {
        const Body result = blendPlanarChains(body, edges, r, false);
        FK_CHECK(checkBody(result).empty());
        TessellationOptions options;
        options.deflection = 0.02;
        FK_CHECK(tessellate(result, options).failedFaces == 0);
        // La faccia superiore perde una striscia di larghezza r lungo il bordo:
        // area tolta / r = perimetro + O(r) (angoli e curvatura).
        const double phi = std::acos((d * d + R * R - a * a) / (2.0 * d * R)), psi = std::acos((d * d + a * a - R * R) / (2.0 * d * a));
        const double perimeter = R * (kTwoPi - 4.0 * phi) + 2.0 * a * (kTwoPi - 2.0 * psi);
        double topArea = 0.0;
        for (FaceId f : result.faces()) {
            const auto *plane = dynamic_cast<const Plane *>(result.face(f).surface.get());
            if (plane && std::fabs(plane->frame().origin().z() - h) < 1e-9 && std::fabs(std::fabs(plane->frame().zDir().z()) - 1.0) < 1e-12)
                topArea += faceArea(result, f);
        }
        FK_CHECK_NEAR((faceArea(body, top) - topArea) / r, perimeter, 0.01 * perimeter);
        // Volume tolto: sezione r^2 (1 - pi/4) lungo il bordo, a meno degli angoli.
        FK_CHECK_NEAR(massProperties(body).volume - massProperties(result).volume, r * r * (1.0 - kPi / 4.0) * perimeter, 0.01 * r * r * perimeter);
    } catch (const std::exception &error) {
        reportFailure(__FILE__, __LINE__, error.what());
    }
}

FK_TEST(BlendArcsIntoConcaveCornersAreExact) {
    // Stesso profilo (disco e due lobi). Le sezioni analitiche scostano gli
    // utensili degli archi oltre le facce: presso un angolo concavo lo
    // scostamento scavava un gradino nella parete accanto (7% di volume in
    // piu' a r = 0.5, 30 facce invece di 10) e il risultato era valido ma
    // sbagliato. Ora quei casi vanno alle catene piane o al raccordo generale.
    const double h = 5.0, d = 19.8, R = 16.0, a = 6.0;
    Body body = makeCylinder(Frame3(Vec3(0, 0, 0), Vec3(0, 0, 1), Vec3(1, 0, 0)), R, h);
    for (double x : {-d, d})
        body = booleanOperation(body, makeCylinder(Frame3(Vec3(x, 0, 0), Vec3(0, 0, 1), Vec3(1, 0, 0)), a, h), BooleanOperation::Unite);
    FaceId top;
    for (FaceId f : body.faces()) {
        const auto *plane = dynamic_cast<const Plane *>(body.face(f).surface.get());
        if (plane && std::fabs(plane->frame().origin().z() - h) < 1e-9 && std::fabs(std::fabs(plane->frame().zDir().z()) - 1.0) < 1e-12) top = f;
    }
    FK_CHECK(top.valid());
    if (!top.valid()) return;
    std::vector<EdgeId> contour, lobe;
    for (LoopId loop : body.face(top).loops)
        for (FinId fin : body.loopFins(loop)) {
            const EdgeId e = body.fin(fin).edge;
            contour.push_back(e);
            const Edge &edge = body.edge(e);
            if (edge.curve->point(0.5 * (edge.range.lo + edge.range.hi)).x() > d) lobe.push_back(e);
        }
    FK_CHECK(contour.size() == 4 && lobe.size() == 1);
    const double volume = massProperties(body).volume;
    try {
        // Tutto il contorno: lo stesso risultato delle catene piane.
        const double r = 0.5;
        const Body result = blendEdges(body, contour, r, false), reference = blendPlanarChains(body, contour, r, false);
        FK_CHECK(checkBody(result).empty());
        FK_CHECK(result.counts().faces == reference.counts().faces);
        FK_CHECK_NEAR(massProperties(result).volume, massProperties(reference).volume, 1e-9 * volume);
    } catch (const std::exception &error) {
        reportFailure(__FILE__, __LINE__, error.what());
    }
    try {
        // L'arco di un lobo da solo, con gli estremi negli angoli concavi.
        const double r = 0.25;
        const Body result = blendEdges(body, lobe, r, false);
        FK_CHECK(checkBody(result).empty());
        FK_CHECK(result.counts().faces == body.counts().faces + 1);
        const double psi = std::acos((d * d + a * a - R * R) / (2.0 * d * a)), length = a * (kTwoPi - 2.0 * psi);
        FK_CHECK_NEAR(volume - massProperties(result).volume, r * r * (1.0 - kPi / 4.0) * length, 0.02 * r * r * length);
        TessellationOptions options;
        options.deflection = 0.02;
        FK_CHECK(tessellate(result, options).failedFaces == 0);
    } catch (const std::exception &error) {
        reportFailure(__FILE__, __LINE__, error.what());
    }
}

FK_TEST(BlendVanishingAtTangentMitreEnd) {
    // Spigolo verticale che prosegue per tangenza nella mitra di due raccordi
    // uguali: la mitra nasce sul coperchio, dove i due raccordi sono tangenti
    // tra loro, e li' il nuovo raccordo svanisce (1476.prt, B173:E112/E458).
    // Prima il fit inseguiva il rumore della sezione mal condizionata e la
    // chiusura cercava una curva tra due contatti coincidenti.
    const double a = 10.0, b = 8.0, c = 6.0, R = 2.0;
    const std::vector<Vec3> top{Vec3(5, 0, c), Vec3(a, 4, c)};
    Body base = makeBox(Frame3(), a, b, c);
    base = blendEdges(base, {nearestEdge(base, top[0], 1e-6), nearestEdge(base, top[1], 1e-6)}, R, false);
    FK_CHECK(checkBody(base).empty());
    const double v0 = massProperties(base).volume;
    const TopoDS_Shape occtBase = occtBlendedShape(BRepPrimAPI_MakeBox(a, b, c).Shape(), top, R, false);
    const double occtV0 = occtBase.IsNull() ? 0.0 : [&] { GProp_GProps p; BRepGProp::VolumeProperties(occtBase, p, 1e-12); return p.Mass(); }();
    for (double r : {0.5, 0.1}) {
        const double volume = blended(base, {Vec3(a, 0, 1)}, r, false, 0.0);
        const double removed = v0 - volume, straight = r * r * (1.0 - kPi / 4.0) * (c - R);
        // Oltre al tratto rettilineo, il tratto lungo la mitra (angolo da 90 gradi a zero).
        FK_CHECK(removed > straight);
        FK_CHECK(removed < straight + r * r * (1.0 - kPi / 4.0) * kPi * R);
        if (occtV0 > 0.0) {
            const double occt = occtBlended(occtBase, {Vec3(a, 0, 1)}, r, false);
            if (occt > 0.0) FK_CHECK_NEAR(removed, occtV0 - occt, 1e-2 * (occtV0 - occt));
        }
    }
}

FK_TEST(BlendArcsMeetingSegments) {
    // Profilo estruso: tre segmenti e un arco che li incontra ad angolo vivo
    // (non tangente). Tutto il bordo in alto (catena piana con gli angoli a
    // mitra), l'arco da solo (i suoi estremi contro fianchi piani obliqui) e il
    // bordo in basso con uno smusso: come OCCT.
    const Vec2 center(3, 1);
    const double radius = distance(center, Vec2(6, 3)), from = std::atan2(2.0, 3.0), to = kPi - from;
    const std::vector<ProfileSegment> profile{lineSegment(Vec2(0, 0), Vec2(6, 0)), lineSegment(Vec2(6, 0), Vec2(6, 3)), arcSegment(center, radius, from, to),
                                              lineSegment(Vec2(0, 3), Vec2(0, 0))};
    const double h = 2.0;
    const Operand block = extrusion(profile, h);
    const std::vector<Vec3> top = edgesAtHeight(block.body, h), bottom = edgesAtHeight(block.body, 0.0);
    const Vec3 onArc(3, 1 + radius, h);
    for (const auto &[points, chamfer] : {std::pair<std::vector<Vec3>, bool>{top, false}, {{onArc}, false}, {{onArc}, true}, {bottom, true}}) {
        const double ours = blended(block.body, points, 0.3, chamfer, 0.0);
        const double occt = occtBlended(block.shape, points, 0.3, chamfer);
        FK_CHECK(occt == 0.0 || std::fabs(ours - occt) < 1e-6 * ours);
    }
}

namespace {

// Profilo con un lato corto S (0.2) tra il fianco B e l'arco C (R 2)
// tangente a S; B e S ad angolo retto nello spigolo K = (10 + dx, 5). Il
// raccordo di raggio 1 dello spigolo tra B e S toccherebbe S a 1 dallo
// spigolo, oltre la sua fine: S e C formano un solo appoggio (tangenti nel
// punto comune), il raccordo e' tangente a B e all'arco C e S sparisce.
// `filleted` e' il profilo atteso (centro a r sopra B e a Rc - r dal centro di C).
struct ShortFaceProfiles {
    std::vector<ProfileSegment> prism, filleted;
    Vec2 corner;
};

ShortFaceProfiles shortFaceProfiles(double dx, double r) {
    const Vec2 O(8 + dx, 5.2), K(10 + dx, 5), A(dx, 0), B(4 + dx, 0), C(4 + dx, 5), T(8 + dx, 7.2), U(dx, 7.2);
    const double Rc = 2.0;
    ShortFaceProfiles result;
    result.corner = K;
    result.prism = {lineSegment(A, B), lineSegment(B, C), lineSegment(C, K), lineSegment(K, Vec2(10 + dx, 5.2)), arcSegment(O, Rc, 0.0, kHalfPi),
                    lineSegment(T, U), lineSegment(U, A)};
    const double lift = K.y() + r - O.y();
    const Vec2 c(O.x() + std::sqrt((Rc - r) * (Rc - r) - lift * lift), K.y() + r);
    const Vec2 tB(c.x(), K.y()), tC = O + Rc * normalized(c - O);
    FK_CHECK(tB.x() < K.x() && tC.x() < K.x() && tC.y() > 5.2);
    result.filleted = {lineSegment(A, B), lineSegment(B, C), lineSegment(C, tB), arcSegment(c, r, -kHalfPi, std::atan2(tC.y() - c.y(), tC.x() - c.x())),
                       arcSegment(O, Rc, std::atan2(tC.y() - O.y(), tC.x() - O.x()), kHalfPi), lineSegment(T, U), lineSegment(U, A)};
    return result;
}

}

FK_TEST(BlendShortFaceJoinsTangentNeighbour) {
    // Prisma (spigolo convesso) e piastra con il prisma come foro (concavo):
    // la stessa zona tolta o aggiunta, con il volume esatto del profilo atteso.
    const double r = 1.0, h = 3.0;
    const ShortFaceProfiles profiles = shortFaceProfiles(0.0, r);
    const Body expected = makeExtrusion(Frame3(), buildProfile(profiles.filleted, 1e-9).regions.front(), h);
    const Operand block = extrusion(profiles.prism, h);
    const double vPrism = massProperties(block.body).volume, vExpected = massProperties(expected).volume;
    FK_CHECK(vExpected < vPrism);
    const Vec3 corner(profiles.corner.x(), profiles.corner.y(), 0.5 * h);
    FK_CHECK_NEAR(blended(block.body, {corner}, r, false, vExpected), vExpected, 1e-9 * vExpected);

    std::vector<ProfileSegment> plate = polygon({Vec2(-5, -5), Vec2(20, -5), Vec2(20, 15), Vec2(-5, 15)});
    std::vector<ProfileSegment> plateExpected = plate;
    plate.insert(plate.end(), profiles.prism.begin(), profiles.prism.end());
    plateExpected.insert(plateExpected.end(), profiles.filleted.begin(), profiles.filleted.end());
    const Body holed = makeExtrusion(Frame3(), buildProfile(plate, 1e-9).regions.front(), h);
    const double vHoledExpected = massProperties(makeExtrusion(Frame3(), buildProfile(plateExpected, 1e-9).regions.front(), h)).volume;
    FK_CHECK_NEAR(blended(holed, {corner}, r, false, vHoledExpected), vHoledExpected, 1e-9 * vHoledExpected);

    // Faccia corta che finisce ad angolo vivo (non tangente) nella faccia
    // accanto: niente appoggio da unire, errore invece di un raccordo sbagliato.
    const std::vector<ProfileSegment> sharp{lineSegment(Vec2(0, 0), Vec2(4, 0)), lineSegment(Vec2(4, 0), Vec2(4, 5)), lineSegment(Vec2(4, 5), Vec2(10, 5)),
                                            lineSegment(Vec2(10, 5), Vec2(10, 5.2)), lineSegment(Vec2(10, 5.2), Vec2(8, 7.2)),
                                            lineSegment(Vec2(8, 7.2), Vec2(0, 7.2)), lineSegment(Vec2(0, 7.2), Vec2(0, 0))};
    const Body sharpBody = extrusion(sharp, h).body;
    FK_CHECK_THROWS(blendEdges(sharpBody, {nearestEdge(sharpBody, corner, 1e-6)}, r, false));
}

FK_TEST(BlendShortFaceOnRevolution) {
    // Lo stesso profilo fatto ruotare attorno all'asse Z (spostato di 5 dall'asse):
    // lo spigolo circolare tra la corona piana (B) e il cilindro corto (S), con
    // il toro di C come appoggio. Volume esatto della rivoluzione del profilo atteso.
    const double r = 1.0;
    const ShortFaceProfiles profiles = shortFaceProfiles(5.0, r);
    const Body solid = makeRevolution(Frame3(), buildProfile(profiles.prism, 1e-9).regions.front());
    const double vExpected = massProperties(makeRevolution(Frame3(), buildProfile(profiles.filleted, 1e-9).regions.front())).volume;
    FK_CHECK(vExpected < massProperties(solid).volume);
    blended(solid, {Vec3(profiles.corner.x(), 0.0, profiles.corner.y())}, r, false, vExpected);
}

FK_TEST(BlendConcaveCornerPocket) {
    // Tasca rettangolare passante: gli spigoli verticali sono concavi per il
    // solido, quelli del bordo in alto convessi. Nell'angolo tra due bordi scelti
    // i due raccordi si incontrano a mitra: oltre gli spigoli, dietro entrambe le
    // pareti, si toglie il materiale sopra tutti e due (per angolo r^3 (5/3 - pi/2)
    // col raccordo, r^3 / 3 con lo smusso: l'integrale di min(g(u), g(v)) sul quadrato).
    const double h = 3.0;
    const Body plate = makeBox(Frame3(), 10.0, 8.0, h);
    const Body pocket = booleanOperation(plate, makeBox(Frame3(Vec3(3, 2, -1), Vec3(0, 0, 1), Vec3(1, 0, 0)), 4.0, 3.0, h + 2.0), BooleanOperation::Subtract);
    const double v0 = 80.0 * h - 12.0 * h;
    const std::vector<Vec3> rim{Vec3(5, 2, h), Vec3(7, 3.5, h), Vec3(5, 5, h), Vec3(3, 3.5, h)};
    for (double r : {0.4, 0.5}) {
        const double waste = r * r * (1.0 - kPi / 4.0), corner = r * r * r * (5.0 / 3.0 - kPi / 2.0);
        const double chamferWaste = 0.5 * r * r, chamferCorner = r * r * r / 3.0;
        blended(pocket, rim, r, false, v0 - waste * 14.0 - 4.0 * corner);
        blended(pocket, rim, r, true, v0 - chamferWaste * 14.0 - 4.0 * chamferCorner);
        // Due bordi vicini (catena aperta: gli estremi contro le pareti normali, un angolo).
        blended(pocket, {rim[0], rim[1]}, r, false, v0 - waste * 7.0 - corner);
        blended(pocket, {rim[0], rim[1]}, r, true, v0 - chamferWaste * 7.0 - chamferCorner);
        // Un bordo solo: finisce contro le pareti accanto.
        blended(pocket, {rim[0]}, r, false, v0 - waste * 4.0);
    }
}

FK_TEST(BlendConcaveCornerPolygons) {
    // Tasche poligonali con gli angoli concavi ottusi (esagono) e acuti
    // (triangolo): tutto il bordo e un lato solo (che finisce contro le pareti
    // oblique accanto: il raccordo arriva fino a loro), come OCCT.
    const double h = 3.0, r = 0.4;
    const Frame3 below(Vec3(0, 0, -1), Vec3(0, 0, 1), Vec3(1, 0, 0));
    for (const std::vector<Vec2> &corners : {std::vector<Vec2>{Vec2(3, 2), Vec2(7, 2), Vec2(8.5, 4), Vec2(7, 6), Vec2(3, 6), Vec2(1.5, 4)},
                                             std::vector<Vec2>{Vec2(2, 2), Vec2(8, 2), Vec2(4, 6)}}) {
        const ProfileRegion region = buildProfile(polygon(corners), 1e-9).regions.front();
        const Body pocket = booleanOperation(makeBox(Frame3(), 10.0, 8.0, h), makeExtrusion(below, region, h + 2.0), BooleanOperation::Subtract);
        const TopoDS_Shape occtPocket = BRepAlgoAPI_Cut(BRepPrimAPI_MakeBox(10.0, 8.0, h).Shape(),
                                                        BRepPrimAPI_MakePrism(occtFace(region, below), gp_Vec(0, 0, h + 2.0)).Shape()).Shape();
        std::vector<Vec3> rim;
        for (std::size_t i = 0; i < corners.size(); ++i) {
            const Vec2 m = 0.5 * (corners[i] + corners[(i + 1) % corners.size()]);
            rim.push_back(Vec3(m.x(), m.y(), h));
        }
        for (bool chamfer : {false, true})
            for (const std::vector<Vec3> &points : {rim, std::vector<Vec3>{rim.front()}}) {
                const double occt = occtBlended(occtPocket, points, r, chamfer);
                FK_CHECK(occt > 0.0);
                blended(pocket, points, r, chamfer, occt > 0.0 ? occt : 0.0);
            }
    }
}

FK_TEST(BlendConcaveCornerLens) {
    // Tasca a lente (intersezione di due cerchi) passante in una piastra, come
    // un foro tagliato da un altro: i due archi del bordo finiscono negli angoli
    // concavi, ognuno contro il cilindro dell'altro. Riferimento indipendente: le
    // zone dei due bordi come anelli interi (rivoluzione completa della
    // sezione); si toglie l'anello di A dentro il cerchio di B (il bordo di A),
    // quello di B dentro A e negli angoli la parte comune dei due anelli (le
    // mitre: per lo smusso con la booleana tra i due coni, per il raccordo
    // integrata numericamente, perche' la booleana tra i due tori non riesce).
    const double h = 3.0, R = 2.0, offset = 1.5;
    const Body plate = makeBox(Frame3(Vec3(-5, -5, 0), Vec3(0, 0, 1), Vec3(1, 0, 0)), 12.0, 10.0, h);
    auto cylinder = [&](double x) { return makeCylinder(Frame3(Vec3(x, 0, -1), Vec3(0, 0, 1), Vec3(1, 0, 0)), R, h + 2.0); };
    const Body pocket = booleanOperation(plate, booleanOperation(cylinder(0.0), cylinder(offset), BooleanOperation::Intersect), BooleanOperation::Subtract);
    const double v0 = massProperties(pocket).volume;
    const std::vector<Vec3> rim{Vec3(R, 0, h), Vec3(offset - R, 0, h)};
    for (double r : {0.2, 0.5})
        for (bool chamfer : {false, true}) {
            auto ring = [&](double x) {
                std::vector<ProfileSegment> section{lineSegment(Vec2(R, h), Vec2(R + r, h))};
                if (chamfer) section.push_back(lineSegment(Vec2(R + r, h), Vec2(R, h - r)));
                else section.push_back(arcSegment(Vec2(R + r, h - r), r, kHalfPi, kPi));
                section.push_back(lineSegment(Vec2(R, h - r), Vec2(R, h)));
                return makeRevolution(Frame3(Vec3(x, 0, 0), Vec3(0, 0, 1), Vec3(1, 0, 0)), buildProfile(section, 1e-9).regions.front());
            };
            const Body ringA = ring(0.0), ringB = ring(offset);
            auto common = [&](const Body &a, const Body &b) { return massProperties(booleanOperation(a, b, BooleanOperation::Intersect)).volume; };
            double mitre = 0.0;
            if (chamfer) {
                mitre = common(ringA, ringB);
            } else {
                // Altezza tolta dall'anello a distanza d fuori dal suo cerchio; nella
                // parte comune la minore delle due. Gauss 4 x 4 su 800 x 800 celle attorno all'angolo.
                auto height = [&](double d) { return d > 0.0 && d < r ? r - std::sqrt(r * r - (r - d) * (r - d)) : 0.0; };
                const double gx[4] = {-0.8611363115940526, -0.3399810435848563, 0.3399810435848563, 0.8611363115940526};
                const double gw[4] = {0.3478548451374538, 0.6521451548625461, 0.6521451548625461, 0.3478548451374538};
                const double cy = std::sqrt(R * R - 0.25 * offset * offset), x0 = 0.5 * offset - 4.0 * r, y0 = cy - r, step = 8.0 * r / 800.0;
                for (int i = 0; i < 800; ++i)
                    for (int j = 0; j < 800; ++j)
                        for (int a = 0; a < 4; ++a)
                            for (int b = 0; b < 4; ++b) {
                                const double x = x0 + (i + 0.5 + 0.5 * gx[a]) * step, y = y0 + (j + 0.5 + 0.5 * gx[b]) * step;
                                const double dA = std::hypot(x, y) - R, dB = std::hypot(x - offset, y) - R;
                                mitre += 0.25 * gw[a] * gw[b] * step * step * std::min(height(dA), height(dB));
                            }
                mitre *= 2.0;  // i due angoli, simmetrici rispetto a y = 0
            }
            const double reference = v0 - common(ringA, cylinder(offset)) - common(ringB, cylinder(0.0)) - mitre;
            const double ours = blended(pocket, rim, r, chamfer, chamfer ? reference : 0.0);
            if (!chamfer) FK_CHECK_NEAR(ours, reference, 3e-6);
        }
}

FK_TEST(BlendConcaveCornerRoundedPocket) {
    // Come nel documento dell'utente: una tasca con il bordo fatto di archi,
    // raccordi e segmenti tangenti tra loro tranne in un angolo vivo concavo tra
    // due archi (la lente dei due cerchi R 2, tagliata in basso da una retta con
    // due raccordi). Tutto il bordo in alto e' una catena chiusa con
    // quell'angolo. Riferimento: le zone esatte dei tratti (segmenti: sezione per
    // lunghezza; archi: Pappus) fino al vertice, piu' la correzione vicino
    // all'angolo, integrata numericamente sulle altezze: la mitra (la parte
    // comune dei due anelli fuori dai due cerchi) meno le parti delle due zone
    // che stanno dietro la parete dell'altro arco.
    const double h = 3.0, R = 2.0, rho = 0.3, bottom = -1.5;
    const Vec2 cA(0, 0), cB(1.5, 0);
    const double cy = std::sqrt(R * R - 0.75 * 0.75);
    const Vec2 V(0.75, cy);
    const double xc = std::sqrt((R - rho) * (R - rho) - (bottom + rho) * (bottom + rho));
    const Vec2 cRight(xc, bottom + rho), cLeft(cB.x() - xc, bottom + rho);
    const double aRight = std::atan2(cRight.y(), cRight.x()), aLeft = std::atan2(cLeft.y() - cB.y(), cLeft.x() - cB.x());
    const double aVA = std::atan2(V.y(), V.x()), aVB = std::atan2(V.y() - cB.y(), V.x() - cB.x());
    struct Arc {
        Vec2 center;
        double radius, from, to;
    };
    const std::vector<Arc> arcs{{cA, R, aRight, aVA}, {cRight, rho, -kHalfPi, aRight}, {cLeft, rho, aLeft, -kHalfPi}, {cB, R, aVB, aLeft + kTwoPi}};
    std::vector<ProfileSegment> outline{lineSegment(Vec2(cLeft.x(), bottom), Vec2(cRight.x(), bottom))};
    for (const Arc &arc : arcs) outline.push_back(arcSegment(arc.center, arc.radius, arc.from, arc.to));
    const Frame3 below(Vec3(0, 0, -1), Vec3(0, 0, 1), Vec3(1, 0, 0));
    const Body pocket = booleanOperation(makeBox(Frame3(Vec3(-5, -5, 0), Vec3(0, 0, 1), Vec3(1, 0, 0)), 12.0, 10.0, h),
                                         makeExtrusion(below, buildProfile(outline, 1e-9).regions.front(), h + 2.0), BooleanOperation::Subtract);
    const double v0 = massProperties(pocket).volume;
    for (double r : {0.1, 0.2})
        for (bool chamfer : {false, true}) {
            // Zone esatte dei tratti: sezione (area, momento radiale rispetto all'asse) per angolo o lunghezza.
            const double area = chamfer ? 0.5 * r * r : r * r * (1.0 - kPi / 4.0);
            auto moment = [&](double radius) {
                if (chamfer) return 0.5 * r * r * (radius + r / 3.0);
                return r * r * (radius + 0.5 * r) - 0.25 * kPi * r * r * (radius + r - 4.0 * r / (3.0 * kPi));
            };
            double removed = area * (cRight.x() - cLeft.x());
            for (const Arc &arc : arcs) removed += (arc.to - arc.from) * moment(arc.radius);
            // Correzione vicino all'angolo (altezza tolta a distanza d fuori da un cerchio).
            auto height = [&](double d) {
                if (!(d > 0.0 && d < r)) return 0.0;
                return chamfer ? r - d : r - std::sqrt(r * r - (r - d) * (r - d));
            };
            auto cross2d = [](const Vec2 &a, const Vec2 &b) { return a.x() * b.y() - a.y() * b.x(); };
            const double gx[4] = {-0.8611363115940526, -0.3399810435848563, 0.3399810435848563, 0.8611363115940526};
            const double gw[4] = {0.3478548451374538, 0.6521451548625461, 0.6521451548625461, 0.3478548451374538};
            const double x0 = V.x() - 4.0 * r, y0 = V.y() - 2.0 * r, step = 8.0 * r / 800.0;
            double correction = 0.0;
            for (int i = 0; i < 800; ++i)
                for (int j = 0; j < 800; ++j)
                    for (int a = 0; a < 4; ++a)
                        for (int b = 0; b < 4; ++b) {
                            const Vec2 q(x0 + (i + 0.5 + 0.5 * gx[a]) * step, y0 + (j + 0.5 + 0.5 * gx[b]) * step);
                            const double dA = distance(q, cA) - R, dB = distance(q, cB) - R;
                            if (!(dA > 0.0 && dB > 0.0)) continue;  // dentro un cerchio: tasca o bordo dell'altro
                            double value = std::min(height(dA), height(dB));
                            if (cross2d(V - cA, q - cA) < 0.0) value -= height(dA);  // zona di A (lato dell'arco) dietro B
                            if (cross2d(V - cB, q - cB) > 0.0) value -= height(dB);  // zona di B dietro A
                            correction += 0.25 * gw[a] * gw[b] * step * step * value;
                        }
            const double reference = v0 - removed - correction;
            const double ours = blended(pocket, {Vec3(R, 0, h)}, r, chamfer, 0.0);  // un arco: la catena prosegue per tangenza
            FK_CHECK_NEAR(ours, reference, 3e-6);
        }
}

FK_TEST(BlendSurfaceEndsOnConcaveCircularWall) {
    // Bordo rettilineo che termina su una parete cilindrica concava:
    // il contatto sul coperchio deve prolungare l'arco, non fermarsi
    // sul vecchio vertice (scanalatura di 1.prt, Raccordo 23).
    const std::vector<ProfileSegment> profile{
        lineSegment(Vec2(-5, -5), Vec2(5, -5)),
        lineSegment(Vec2(5, -5), Vec2(5, 0)),
        lineSegment(Vec2(5, 0), Vec2(1, 0)),
        arcSegment(Vec2(0, -1), std::sqrt(2.0), kPi / 4.0, 3.0 * kPi / 4.0),
        lineSegment(Vec2(-1, 0), Vec2(-5, 0)),
        lineSegment(Vec2(-5, 0), Vec2(-5, -5))};
    const Operand part = extrusion(profile, 4.0);
    for (const Vec3 &point : {Vec3(-3, 0, 4), Vec3(3, 0, 4)})
        for (double radius : {0.1, 0.5}) {
            const double expected = occtBlended(part.shape, {point}, radius, false);
            FK_CHECK(expected > 0.0);
            surfaceBlended(part.body, {point}, radius, false, expected);
        }
}
