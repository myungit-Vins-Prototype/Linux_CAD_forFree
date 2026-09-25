#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepPrimAPI_MakeRevol.hxx>
#include <gp_Ax1.hxx>
#include <GProp_GProps.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Tool.hxx>
#include <Poly_Triangulation.hxx>
#include <TopExp_Explorer.hxx>

#include "fk_body_check.h"
#include "fk_boolean.h"
#include "fk_extrude.h"
#include "fk_revolve.h"
#include "fk_marching.h"
#include "fk_mass.h"
#include "fk_primitives.h"
#include "fk_quadrature.h"
#include "fk_test_profiles.h"

using namespace fktest;

namespace {

gp_Ax2 toAx2(const Frame3 &frame) { return gp_Ax2(toPnt(frame.origin()), toDir(frame.zDir()), toDir(frame.xDir())); }

// Solido del nuovo kernel e lo stesso in OCCT.
struct Operand {
    Body body;
    TopoDS_Shape shape;
};

Operand box(const Frame3 &frame, double dx, double dy, double dz) {
    return {makeBox(frame, dx, dy, dz), BRepPrimAPI_MakeBox(toAx2(frame), dx, dy, dz).Shape()};
}

Operand cylinder(const Frame3 &frame, double r, double h) {
    return {makeCylinder(frame, r, h), BRepPrimAPI_MakeCylinder(toAx2(frame), r, h).Shape()};
}

Operand extrusion(const Frame3 &frame, const std::vector<ProfileSegment> &segments, double height) {
    const ProfileRegion region = buildProfile(segments, 1e-6).regions.front();
    return {makeExtrusion(frame, region, height),
            BRepPrimAPI_MakePrism(occtFace(region, frame), gp_Vec(toPnt(Vec3()), toPnt(height * frame.zDir()))).Shape()};
}

// Area e volume da una tassellazione fine di OCCT: solo un terzo parere,
// quando BRepGProp non e' affidabile (facce estruse da spline tagliate: la
// sua area sbaglia anche dello 0.06%, la tassellazione fine no).
void meshProperties(const TopoDS_Shape &shape, double &area, double &volume) {
    BRepMesh_IncrementalMesh mesh(shape, 5e-4, false, 0.05);
    area = volume = 0.0;
    for (TopExp_Explorer faces(shape, TopAbs_FACE); faces.More(); faces.Next()) {
        const TopoDS_Face &face = TopoDS::Face(faces.Current());
        TopLoc_Location location;
        const Handle(Poly_Triangulation) triangulation = BRep_Tool::Triangulation(face, location);
        for (int t = 1; t <= triangulation->NbTriangles(); ++t) {
            int n1, n2, n3;
            triangulation->Triangle(t).Get(n1, n2, n3);
            if (face.Orientation() == TopAbs_REVERSED) std::swap(n2, n3);
            const gp_Pnt p1 = triangulation->Node(n1).Transformed(location), p2 = triangulation->Node(n2).Transformed(location),
                         p3 = triangulation->Node(n3).Transformed(location);
            area += 0.5 * gp_Vec(p1, p2).Crossed(gp_Vec(p1, p3)).Magnitude();
            volume += gp_Vec(p1.XYZ()).Dot(gp_Vec(p2.XYZ()).Crossed(gp_Vec(p3.XYZ()))) / 6.0;
        }
    }
}

TopoDS_Shape occtBoolean(const TopoDS_Shape &a, const TopoDS_Shape &b, BooleanOperation operation) {
    switch (operation) {
    case BooleanOperation::Unite: return BRepAlgoAPI_Fuse(a, b).Shape();
    case BooleanOperation::Intersect: return BRepAlgoAPI_Common(a, b).Shape();
    default: return BRepAlgoAPI_Cut(a, b).Shape();
    }
}

const char *name(BooleanOperation operation) {
    switch (operation) {
    case BooleanOperation::Unite: return "unione";
    case BooleanOperation::Intersect: return "intersezione";
    default: return "differenza";
    }
}

// Booleana del nuovo kernel (valida per costruzione: passa da checkBody)
// contro BRepAlgoAPI: volume, area e baricentro. `exactOcct`: facce solo
// piane e cilindriche, dove BRepGProp e' affidabile; altrimenti area e
// volume si confrontano con la tassellazione fine del risultato OCCT.
void compare(const Operand &a, const Operand &b, BooleanOperation operation, bool exactOcct = true, double relative = 1e-8) {
    Body result;
    try {
        result = booleanOperation(a.body, b.body, operation);
    } catch (const std::exception &error) {
        reportFailure(__FILE__, __LINE__, std::string(name(operation)) + ": " + error.what());
        return;
    }
    const TopoDS_Shape reference = occtBoolean(a.shape, b.shape, operation);
    GProp_GProps volume, surface;
    BRepGProp::VolumeProperties(reference, volume, 1e-12);
    BRepGProp::SurfaceProperties(reference, surface, 1e-12);
    if (volume.Mass() < 1e-9) {
        FK_CHECK(result.faces().empty());
        return;
    }
    const MassProperties ours = massProperties(result);
    if (exactOcct) {
        FK_CHECK_NEAR(ours.volume, volume.Mass(), relative * volume.Mass());
        FK_CHECK_NEAR(ours.area, surface.Mass(), relative * surface.Mass());
        FK_CHECK(distance(ours.centroid, fromOcct(volume.CentreOfMass())) <= relative * std::cbrt(volume.Mass()));
    } else {
        double meshArea, meshVolume;
        meshProperties(reference, meshArea, meshVolume);
        FK_CHECK_NEAR(ours.volume, meshVolume, 2e-4 * meshVolume);
        FK_CHECK_NEAR(ours.area, meshArea, 5e-5 * meshArea);
        FK_CHECK(distance(ours.centroid, fromOcct(volume.CentreOfMass())) <= 1e-2 * std::cbrt(volume.Mass()));
    }
}

void compareAll(const Operand &a, const Operand &b, bool exactOcct = true, double relative = 1e-8) {
    for (BooleanOperation operation : {BooleanOperation::Unite, BooleanOperation::Intersect, BooleanOperation::Subtract})
        compare(a, b, operation, exactOcct, relative);
}

// Frame ruotato attorno a un punto, con orientamento casuale.
Frame3 rotatedFrame(std::mt19937 &rng, const Vec3 &origin) {
    return Frame3(origin, randomDirection(rng), randomDirection(rng));
}

}

// Parallelepipedi in posizione generica (nessuna faccia complanare).
FK_TEST(BooleanBoxesGeneralPosition) {
    std::mt19937 rng(500);
    for (int trial = 0; trial < 12; ++trial) {
        const Frame3 frameA = randomFrame(rng, 20.0);
        const Operand a = box(frameA, uniform(rng, 10, 30), uniform(rng, 10, 30), uniform(rng, 10, 30));
        const Operand b = box(rotatedFrame(rng, frameA.toGlobal(Vec3(uniform(rng, 0, 15), uniform(rng, 0, 15), uniform(rng, 0, 15)))),
                              uniform(rng, 5, 25), uniform(rng, 5, 25), uniform(rng, 5, 25));
        compareAll(a, b);
    }
}

// Facce complanari: stesso piano di schizzo, blocchi affiancati, a filo,
// tasca aperta sul fondo, unione che si tocca solo su una faccia.
FK_TEST(BooleanCoplanarBoxes) {
    const Frame3 frame(Vec3(1, 2, 3), Vec3(0.3, -0.2, 1), Vec3(1, 0.5, 0));
    const Operand a = box(frame, 20, 10, 5);
    compareAll(a, box(Frame3(frame.toGlobal(Vec3(12, 3, 0)), frame.zDir(), frame.xDir()), 15, 4, 5));   // stesso fondo e cima
    compareAll(a, box(Frame3(frame.toGlobal(Vec3(5, 2, 0)), frame.zDir(), frame.xDir()), 6, 4, 2));     // tasca dal fondo
    compareAll(a, box(Frame3(frame.toGlobal(Vec3(5, -3, 2)), frame.zDir(), frame.xDir()), 6, 20, 1));   // attraversa due fianchi
    compare(a, box(Frame3(frame.toGlobal(Vec3(4, 3, 5)), frame.zDir(), frame.xDir()), 5, 5, 5), BooleanOperation::Unite);  // appoggiato sopra
    compareAll(a, box(Frame3(frame.toGlobal(Vec3(0, 0, 0)), frame.zDir(), frame.xDir()), 20, 10, 8));   // stessa base, piu' alto
}

// Un solido dentro l'altro (nessuna intersezione) e solidi disgiunti.
FK_TEST(BooleanNestedAndDisjoint) {
    const Operand a = box(Frame3(), 20, 20, 20);
    compareAll(a, box(Frame3(Vec3(5, 5, 5), Vec3(1, 1, 1), Vec3(1, 0, 0)), 4, 4, 4));
    compareAll(a, box(Frame3(Vec3(40, 0, 0), Vec3(0, 0, 1), Vec3(1, 0, 0)), 5, 5, 5));
}

// Parallelepipedo e cilindro: fori passanti e ciechi (cerchi), cilindro
// obliquo (ellissi), cilindro parallelo a due facce (generatrici).
FK_TEST(BooleanBoxAndCylinder) {
    std::mt19937 rng(501);
    const Operand a = box(Frame3(), 30, 20, 10);
    compareAll(a, cylinder(Frame3(Vec3(10, 10, -5), Vec3(0, 0, 1), Vec3(1, 0, 0)), 4, 20));     // foro passante
    compareAll(a, cylinder(Frame3(Vec3(20, 8, 4), Vec3(0, 0, 1), Vec3(1, 0, 0)), 3, 20));       // cieco dall'alto
    compareAll(a, cylinder(Frame3(Vec3(-5, 10, 5), Vec3(1, 0, 0), Vec3(0, 1, 0)), 3, 40));      // lungo X
    compareAll(a, cylinder(Frame3(Vec3(15, 10, 12), Vec3(1, 0, 0), Vec3(0, 1, 0)), 6, 10));     // sporge dalla cima
    compare(a, cylinder(Frame3(Vec3(12, 9, 10), Vec3(0, 0, 1), Vec3(1, 0, 0)), 3, 6), BooleanOperation::Unite);  // appoggiato sopra
    compareAll(a, cylinder(Frame3(Vec3(12, 9, 0), Vec3(0, 0, 1), Vec3(1, 0, 0)), 3, 5));        // tasca dal fondo (disco complanare)
    compareAll(a, cylinder(Frame3(Vec3(28, 9, 0), Vec3(0, 0, 1), Vec3(1, 0, 0)), 5, 10));       // stesso fondo e cima, sporge di lato
    for (int trial = 0; trial < 6; ++trial) {
        const Vec3 center(uniform(rng, 8, 22), uniform(rng, 6, 14), uniform(rng, -10, -6));
        Vec3 axis = randomDirection(rng);
        axis = normalized(Vec3(0.4 * axis.x(), 0.4 * axis.y(), 1.0));
        compareAll(a, cylinder(Frame3(center, axis, Vec3(1, 0, 0)), uniform(rng, 1.5, 4.0), 30));  // obliquo
    }
}

// Estrusioni con spline e NURBS contro parallelepipedi: sezioni oblique dei
// fianchi estrusi (NURBS esatte) e generatrici.
FK_TEST(BooleanExtrusionAndBox) {
    std::mt19937 rng(502);
    std::vector<ProfileSegment> profile = roundedRectangle(Vec2(-10, -6), 20.0, 12.0, 2.5);
    profile.push_back(closedSpline(Vec2(4, 0), 2.0, true));
    const Operand a = extrusion(Frame3(), profile, 8.0);
    for (int trial = 0; trial < 6; ++trial) {
        const Frame3 frame = rotatedFrame(rng, Vec3(uniform(rng, -8, 2), uniform(rng, -8, 0), uniform(rng, -2, 4)));
        compareAll(a, box(frame, uniform(rng, 6, 14), uniform(rng, 6, 14), uniform(rng, 6, 14)), false);
    }
    // Spline aperta chiusa da due segmenti, tagliata da un blocco parallelo all'estrusione.
    auto spline = std::make_shared<BSplineCurve<2>>(3, std::vector<double>{0, 0, 0, 0, 0.5, 1, 1, 1, 1},
                                                    std::vector<Vec2>{Vec2(-5, 0), Vec2(-4, 4), Vec2(0, 6), Vec2(4, 2), Vec2(5, 0)});
    const Operand c = extrusion(Frame3(), {{spline, spline->domain()}, lineSegment(Vec2(5, 0), Vec2(0, -3)), lineSegment(Vec2(0, -3), Vec2(-5, 0))}, 6.0);
    compareAll(c, box(Frame3(Vec3(-1, -5, -2), Vec3(0, 0, 1), Vec3(1, 0.2, 0)), 3, 15, 10), false);

    // Identita' dei volumi con le nostre sole proprieta' di massa.
    const Operand b = box(rotatedFrame(rng, Vec3(-3, -4, 1)), 10, 9, 12);
    const double va = massProperties(a.body).volume, vb = massProperties(b.body).volume;
    const double vu = massProperties(booleanOperation(a.body, b.body, BooleanOperation::Unite)).volume;
    const double vi = massProperties(booleanOperation(a.body, b.body, BooleanOperation::Intersect)).volume;
    const double vd = massProperties(booleanOperation(a.body, b.body, BooleanOperation::Subtract)).volume;
    FK_CHECK_NEAR(vu, va + vb - vi, 1e-9 * vu);
    FK_CHECK_NEAR(vd, va - vi, 1e-9 * va);
}

// Booleane in cascata: il risultato di una booleana come operando.
FK_TEST(BooleanChained) {
    const Operand a = box(Frame3(), 30, 20, 10);
    const Operand hole = cylinder(Frame3(Vec3(10, 10, -5), Vec3(0, 0, 1), Vec3(1, 0, 0)), 4, 20);
    const Operand slot = box(Frame3(Vec3(18, -1, 6), Vec3(0, 0, 1), Vec3(1, 0, 0)), 5, 22, 10);
    const Body first = booleanOperation(a.body, hole.body, BooleanOperation::Subtract);
    const Body second = booleanOperation(first, slot.body, BooleanOperation::Subtract);
    const TopoDS_Shape reference = BRepAlgoAPI_Cut(BRepAlgoAPI_Cut(a.shape, hole.shape).Shape(), slot.shape).Shape();
    GProp_GProps properties;
    BRepGProp::VolumeProperties(reference, properties, 1e-12);
    FK_CHECK_NEAR(massProperties(second).volume, properties.Mass(), 1e-8 * properties.Mass());
}

namespace {

// Volume comune a due cilindri infiniti di raggi r <= R con assi
// perpendicolari incidenti: 8 int_0^r sqrt(r^2 - x^2) sqrt(R^2 - x^2) dx
// (sezioni rettangolari), con x = r sin(t) e Gauss-Legendre composta.
double crossedCylindersVolume(double r, double R) {
    const double nodes[5] = {-0.9061798459386640, -0.5384693101056831, 0.0, 0.5384693101056831, 0.9061798459386640};
    const double weights[5] = {0.2369268850561891, 0.4786286704993665, 0.5688888888888889, 0.4786286704993665, 0.2369268850561891};
    const int panels = 400;
    double sum = 0.0;
    for (int p = 0; p < panels; ++p) {
        const double a = kHalfPi * p / panels, h = kHalfPi / panels;
        for (int j = 0; j < 5; ++j) {
            const double t = a + 0.5 * h * (nodes[j] + 1.0), s = std::sin(t), c = std::cos(t);
            sum += 0.5 * h * weights[j] * r * r * c * c * std::sqrt(R * R - r * r * s * s);
        }
    }
    return 8.0 * sum;
}

}

// Cilindro contro cilindro (curve d'intersezione tracciate): attraversamento
// completo con il volume esatto, innesto a T, assi sghembi e obliqui, assi
// paralleli (generatrici esatte), contro BRepAlgoAPI.
FK_TEST(BooleanCylinderCylinder) {
    const Frame3 frame(Vec3(1, -2, 3), Vec3(0.2, 0.1, 1), Vec3(1, 0, 0));
    auto local = [&](double x, double y, double z) { return frame.toGlobal(Vec3(x, y, z)); };
    const Operand a = cylinder(Frame3(local(0, 0, -10), frame.zDir(), frame.xDir()), 5, 20);
    const Operand through = cylinder(Frame3(local(-10, 0, 0), frame.xDir(), frame.yDir()), 3, 20);
    compareAll(a, through, true, 1e-6);
    const double exact = crossedCylindersVolume(3, 5);
    FK_CHECK_NEAR(massProperties(booleanOperation(a.body, through.body, BooleanOperation::Intersect)).volume, exact, 1e-9 * exact);

    compareAll(a, cylinder(Frame3(local(0, 0, 2), frame.xDir(), frame.yDir()), 2, 12), true, 1e-6);    // innesto a T
    compareAll(a, cylinder(Frame3(local(-10, 1.5, 1), frame.xDir(), frame.yDir()), 2.5, 20), true, 1e-6);  // assi sghembi
    const Vec3 oblique = normalized(frame.xDir() + 0.4 * frame.zDir() + 0.3 * frame.yDir());
    compareAll(a, cylinder(Frame3(local(0, 0, 0) - 12.0 * oblique, oblique, frame.zDir()), 2, 24), true, 1e-6);  // obliquo
    compareAll(a, cylinder(Frame3(local(3, 2, -4), frame.zDir(), frame.xDir()), 3, 10), true, 1e-8);    // assi paralleli

    // Due fori incrociati in un blocco, uno dopo l'altro.
    const Operand block = box(Frame3(local(-8, -8, -6), frame.zDir(), frame.xDir()), 16, 16, 12);
    const Operand holeZ = cylinder(Frame3(local(0, 0, -10), frame.zDir(), frame.xDir()), 4, 20);
    const Operand holeX = cylinder(Frame3(local(-10, 0, 1), frame.xDir(), frame.yDir()), 3, 20);
    const Body drilled = booleanOperation(booleanOperation(block.body, holeZ.body, BooleanOperation::Subtract), holeX.body,
                                          BooleanOperation::Subtract);
    const TopoDS_Shape reference = BRepAlgoAPI_Cut(BRepAlgoAPI_Cut(block.shape, holeZ.shape).Shape(), holeX.shape).Shape();
    GProp_GProps properties;
    BRepGProp::VolumeProperties(reference, properties, 1e-12);
    FK_CHECK_NEAR(massProperties(drilled).volume, properties.Mass(), 1e-6 * properties.Mass());
}

// Fianchi estrusi da spline contro cilindri e contro altri fianchi estrusi in
// un'altra direzione.
FK_TEST(BooleanCurvedExtrusions) {
    std::vector<ProfileSegment> profile = roundedRectangle(Vec2(-10, -6), 20.0, 12.0, 2.5);
    profile.push_back(closedSpline(Vec2(4, 0), 2.0, true));
    const Operand a = extrusion(Frame3(), profile, 8.0);
    // Riferimento: la tassellazione fine di OCCT (lenta: un'operazione per caso, tranne il primo).
    compareAll(a, cylinder(Frame3(Vec3(-14, 0.5, 4), Vec3(1, 0.1, 0.05), Vec3(0, 0, 1)), 2.5, 30), false);  // attraversa il foro a spline
    compare(a, cylinder(Frame3(Vec3(4, -9, 3.5), Vec3(0.1, 1, 0.2), Vec3(0, 0, 1)), 1.5, 20), BooleanOperation::Subtract, false);
    const ProfileSegment spline = closedSpline(Vec2(0, 0), 3.0, false);
    const Operand b = extrusion(Frame3(Vec3(-2, 1, 4), Vec3(1, 0.2, 0.1), Vec3(0, 0, 1)), {spline}, 14.0);
    const Operand c = extrusion(Frame3(Vec3(-12, 0, 4), Vec3(1, 0, 0), Vec3(0, 0, 1)), {spline}, 24.0);
    compare(a, c, BooleanOperation::Intersect, false);  // spigoli vivi delle due spline chiuse che si incrociano
    compare(b, cylinder(Frame3(Vec3(3, -6, 4.5), Vec3(0, 1, 0.1), Vec3(1, 0, 0)), 1.2, 14), BooleanOperation::Unite, false);

    // Identita' dei volumi con le nostre sole proprieta' di massa.
    const double va = massProperties(a.body).volume, vc = massProperties(c.body).volume;
    const double vu = massProperties(booleanOperation(a.body, c.body, BooleanOperation::Unite)).volume;
    const double vi = massProperties(booleanOperation(a.body, c.body, BooleanOperation::Intersect)).volume;
    const double vd = massProperties(booleanOperation(a.body, c.body, BooleanOperation::Subtract)).volume;
    FK_CHECK_NEAR(vu, va + vc - vi, 1e-9 * vu);
    FK_CHECK_NEAR(vd, va - vi, 1e-9 * va);
}

// Coppie non ancora gestite: errore esplicito, non un risultato sbagliato.
namespace {

// Solido di rivoluzione del profilo (piano XZ di frame, asse Z) e il suo gemello OCCT.
Operand revolved(const Frame3 &frame, const std::vector<ProfileSegment> &segments) {
    const ProfileRegion region = buildProfile(segments, 1e-9).regions.front();
    const Frame3 profilePlane(frame.origin(), -frame.yDir(), frame.xDir());
    return {makeRevolution(frame, region),
            BRepPrimAPI_MakeRevol(occtFace(region, profilePlane), gp_Ax1(toPnt(frame.origin()), toDir(frame.zDir()))).Shape()};
}

}

// Sfere, coni e tori (solidi di rivoluzione) contro blocchi e cilindri:
// piani e cilindri con superfici di ogni tipo (semi dalla suddivisione).
FK_TEST(BooleanRevolvedSolids) {
    const Frame3 frame(Vec3(1, -2, 3), Vec3(0.2, 0.1, 1), Vec3(1, 0, 0));
    auto local = [&](double x, double y, double z) { return frame.toGlobal(Vec3(x, y, z)); };
    const Operand sphere = revolved(frame, {arcSegment(Vec2(0, 0), 4.0, -kHalfPi, kHalfPi), lineSegment(Vec2(0, 4), Vec2(0, -4))});
    const Operand block = box(Frame3(local(1, -6, -6), frame.zDir(), frame.xDir()), 10, 12, 12);
    compareAll(sphere, block);
    const Operand hole = cylinder(Frame3(local(0.5, 0.3, -8), frame.zDir(), frame.xDir()), 1.5, 16);
    compareAll(sphere, hole);
    const Operand cone = revolved(frame, {lineSegment(Vec2(0, -3), Vec2(4, -3)), lineSegment(Vec2(4, -3), Vec2(0, 5)), lineSegment(Vec2(0, 5), Vec2(0, -3))});
    compareAll(cone, block);
    const Operand torus = revolved(frame, {arcSegment(Vec2(5, 0), 1.5, 0.0, kTwoPi)});
    compareAll(torus, block);
    compareAll(torus, cylinder(Frame3(local(-8, 5, 0.3), frame.xDir(), frame.yDir()), 1.0, 16));
}

// Superfici non piane coincidenti: cilindri coassiali dello stesso raggio
// (sovrapposti, impilati, uno dentro l'altro, con parametrizzazioni
// diverse), un foro riempito da un perno dello stesso raggio, fianchi estrusi
// dalla stessa spline chiusa.
FK_TEST(BooleanCoincidentSurfaces) {
    const Frame3 frame(Vec3(1, -2, 3), Vec3(0.2, 0.1, 1), Vec3(1, 0, 0));
    auto at = [&](double x, double y, double z) { return Frame3(frame.toGlobal(Vec3(x, y, z)), frame.zDir(), frame.xDir()); };
    const Operand a = cylinder(at(0, 0, 0), 5, 20);
    compareAll(a, cylinder(at(0, 0, 10), 5, 20));   // sovrapposti
    compareAll(a, cylinder(at(0, 0, 20), 5, 10));   // impilati: si toccano nel cerchio
    compareAll(a, cylinder(at(0, 0, 5), 5, 10));    // uno dentro l'altro, stesso fianco
    compareAll(a, cylinder(at(0, 0, -5), 5, 30));   // l'altro sporge dalle due parti
    compareAll(a, cylinder(at(0, 0, 0), 5, 20));    // identici
    compareAll(a, cylinder(Frame3(frame.toGlobal(Vec3(0, 0, 8)), frame.zDir(), frame.yDir()), 5, 20));  // u spostato
    compareAll(a, cylinder(Frame3(frame.toGlobal(Vec3(0, 0, 25)), -frame.zDir(), frame.xDir()), 5, 15));  // asse opposto

    // Foro e perno dello stesso raggio.
    const Operand block = box(at(-8, -8, 0), 16, 16, 12);
    const Operand hole = cylinder(at(2, 1, -5), 3, 30);
    const Operand drilled{booleanOperation(block.body, hole.body, BooleanOperation::Subtract), BRepAlgoAPI_Cut(block.shape, hole.shape).Shape()};
    compareAll(drilled, cylinder(at(2, 1, 4), 3, 20));
    compareAll(drilled, cylinder(at(2, 1, 3), 3, 5));

    // Spigoli di un solido che giacciono sul fianco curvo dell'altro: un
    // segmento di disco (arco + corda) e un cilindro che passa per i due
    // vertici dell'arco; poi una spline chiusa per quei punti.
    const double c = std::cos(kPi / 3.0) * 5.0, h = std::sin(kPi / 3.0) * 5.0;
    const std::vector<ProfileSegment> segmentOfDisc{arcSegment(Vec2(0, 0), 5.0, -kPi / 3.0, kPi / 3.0), lineSegment(Vec2(c, h), Vec2(c, -h))};
    const Operand disc = extrusion(at(0, 0, 0), segmentOfDisc, 8);
    compareAll(disc, cylinder(at(c + 3.0, 0, -2), std::hypot(3.0, h), 12));
    compareAll(disc, cylinder(at(c - 3.0, 0, 2), std::hypot(3.0, h), 12));

    // Fianchi estrusi dalla stessa spline chiusa.
    const std::vector<ProfileSegment> profile{closedSpline(Vec2(0, 0), 4.0, true)};
    compareAll(extrusion(at(0, 0, 0), profile, 6), extrusion(at(0, 0, 3), profile, 6), false);
}

namespace {

// --- Volumi esatti per le configurazioni allineate agli assi -----------------

double overlap(double a0, double a1, double b0, double b1) { return std::max(0.0, std::min(a1, b1) - std::max(a0, b0)); }

// Integrale di f su [lo, hi] spezzato nei punti dati (dove f non e' liscia).
template <class F>
double integratePieces(const F &f, double lo, double hi, std::vector<double> breaks) {
    if (!(hi > lo)) return 0.0;
    breaks.push_back(lo);
    breaks.push_back(hi);
    std::sort(breaks.begin(), breaks.end());
    double total = 0.0, previous = lo;
    for (double b : breaks) {
        if (b <= previous || b > hi) continue;
        total += ForgeCad::Kernel::detail::integrate(f, previous, b, 1e-12);
        previous = b;
    }
    return total;
}

// Ascisse in cui sqrt(r^2 - (x - c)^2) = d.
void sqrtLevels(double c, double r, double d, std::vector<double> &out) {
    if (std::fabs(d) < r) {
        const double w = std::sqrt(r * r - d * d);
        out.push_back(c - w);
        out.push_back(c + w);
    }
}

// Area del disco (centro c, raggio r) dentro il rettangolo [x0, x1] x [y0, y1].
double discRectangleArea(const Vec2 &c, double r, double x0, double x1, double y0, double y1) {
    std::vector<double> breaks;
    sqrtLevels(c.x(), r, c.y() - y0, breaks);
    sqrtLevels(c.x(), r, y1 - c.y(), breaks);
    auto f = [&](double x) {
        const double h = std::sqrt(std::max(0.0, r * r - (x - c.x()) * (x - c.x())));
        return overlap(c.y() - h, c.y() + h, y0, y1);
    };
    return integratePieces(f, std::max(x0, c.x() - r), std::min(x1, c.x() + r), breaks);
}

// Area comune a due dischi (lente, forma chiusa).
double discDiscArea(const Vec2 &c1, double r1, const Vec2 &c2, double r2) {
    const double d = distance(c1, c2);
    if (d >= r1 + r2) return 0.0;
    if (d <= std::fabs(r1 - r2)) return kPi * std::pow(std::min(r1, r2), 2);
    const double a1 = std::acos((d * d + r1 * r1 - r2 * r2) / (2 * d * r1)), a2 = std::acos((d * d + r2 * r2 - r1 * r1) / (2 * d * r2));
    return r1 * r1 * (a1 - std::sin(2 * a1) / 2) + r2 * r2 * (a2 - std::sin(2 * a2) / 2);
}

// Volume comune al cilindro A (asse z per l'origine, raggio ra, z in [0, ha])
// e al cilindro B con asse lungo x: (y - yc)^2 + (z - zc)^2 <= rb^2, x in [x0, x1].
// Per sezioni y = costante: rettangolo comune in (x, z).
double crossedCylinderVolume(double ra, double ha, double yc, double zc, double rb, double x0, double x1) {
    std::vector<double> breaks;
    sqrtLevels(0.0, ra, x0, breaks);
    sqrtLevels(0.0, ra, x1, breaks);
    sqrtLevels(yc, rb, zc, breaks);
    sqrtLevels(yc, rb, ha - zc, breaks);
    auto f = [&](double y) {
        const double sa = std::sqrt(std::max(0.0, ra * ra - y * y)), sb = std::sqrt(std::max(0.0, rb * rb - (y - yc) * (y - yc)));
        return overlap(-sa, sa, x0, x1) * overlap(zc - sb, zc + sb, 0.0, ha);
    };
    return integratePieces(f, std::max(-ra, yc - rb), std::min(ra, yc + rb), breaks);
}

double expectedVolume(double va, double vb, double common, BooleanOperation op) {
    switch (op) {
    case BooleanOperation::Unite: return va + vb - common;
    case BooleanOperation::Intersect: return common;
    default: return va - common;
    }
}

// Esito di una booleana contro il volume esatto: "" se torna, altrimenti il problema.
std::string stressCase(const Operand &a, const Operand &b, BooleanOperation op, double expected) {
    try {
        const Body result = booleanOperation(a.body, b.body, op);
        const double ours = result.faces().empty() ? 0.0 : massProperties(result).volume;
        if (std::fabs(ours - expected) > 1e-7 * std::max(1.0, expected))
            return "volume " + std::to_string(ours) + " invece di " + std::to_string(expected);
        return "";
    } catch (const std::exception &e) {
        return std::string("eccezione: ") + e.what();
    }
}

}

// Configurazioni degeneri su una griglia intera: facce complanari, spigoli
// e vertici sulle facce dell'altro solido, cilindri tangenti alle facce
// (dentro e fuori). Riferimento: il volume esatto (OCCT sbaglia l'unione nei
// contatti tangenti, anche con volumi negativi). Nessun risultato sbagliato e
// nessuna eccezione.
FK_TEST(BooleanStressGrid) {
    std::mt19937 rng(777);
    for (int trial = 0; trial < 150; ++trial) {
        auto snap = [&](double lo, double hi) { return std::round(uniform(rng, lo, hi)); };
        const double ax = snap(4, 12), ay = snap(4, 12), az = snap(4, 12);
        const Operand a = box(Frame3(), ax, ay, az);
        Operand b;
        double vb = 0.0, common = 0.0;
        switch (trial % 3) {
        case 0: {
            const Vec3 corner(snap(-4, 8), snap(-4, 8), snap(-4, 8));
            const double bx = snap(2, 10), by = snap(2, 10), bz = snap(2, 10);
            b = box(Frame3(corner, Vec3(0, 0, 1), Vec3(1, 0, 0)), bx, by, bz);
            vb = bx * by * bz;
            common = overlap(0, ax, corner.x(), corner.x() + bx) * overlap(0, ay, corner.y(), corner.y() + by) *
                     overlap(0, az, corner.z(), corner.z() + bz);
            break;
        }
        case 1: {
            const Vec3 base(snap(0, 10), snap(0, 10), snap(-4, 6));
            const double r = snap(1, 4), h = snap(2, 12);
            b = cylinder(Frame3(base, Vec3(0, 0, 1), Vec3(1, 0, 0)), r, h);
            vb = kPi * r * r * h;
            common = discRectangleArea(Vec2(base.x(), base.y()), r, 0, ax, 0, ay) * overlap(0, az, base.z(), base.z() + h);
            break;
        }
        default: {
            const Vec3 base(snap(-4, 2), snap(0, 10), snap(0, 10));
            const double r = snap(1, 4), h = snap(4, 16);
            b = cylinder(Frame3(base, Vec3(1, 0, 0), Vec3(0, 1, 0)), r, h);
            vb = kPi * r * r * h;
            common = discRectangleArea(Vec2(base.y(), base.z()), r, 0, ay, 0, az) * overlap(0, ax, base.x(), base.x() + h);
            break;
        }
        }
        for (BooleanOperation op : {BooleanOperation::Unite, BooleanOperation::Intersect, BooleanOperation::Subtract}) {
            const std::string problem = stressCase(a, b, op, expectedVolume(ax * ay * az, vb, common, op));
            if (!problem.empty()) reportFailure(__FILE__, __LINE__, "caso " + std::to_string(trial) + ": " + problem);
        }
    }
}

// Cilindri su una griglia intera, assi paralleli o perpendicolari: raggi
// uguali (rami che si incrociano), fianchi tangenti dentro e fuori, contatti
// in un punto, cerchi sulle facce dell'altro. Riferimento: il volume esatto;
// nessun risultato sbagliato e nessuna eccezione.
FK_TEST(BooleanStressCylinderGrid) {
    std::mt19937 rng(780);
    for (int trial = 0; trial < 60; ++trial) {
        auto snap = [&](double lo, double hi) { return std::round(uniform(rng, lo, hi)); };
        const double ra = snap(2, 5), ha = snap(4, 10);
        const Operand a = cylinder(Frame3(Vec3(0, 0, 0), Vec3(0, 0, 1), Vec3(1, 0, 0)), ra, ha);
        const Vec3 axes[3] = {Vec3(1, 0, 0), Vec3(0, 1, 0), Vec3(0, 0, 1)};
        const Vec3 axis = axes[trial % 3];
        const Vec3 base = trial % 3 == 2 ? Vec3(snap(-4, 4), snap(-4, 4), snap(-4, 8)) : Vec3(snap(-4, 4), snap(-4, 4), snap(0, 10)) - 8.0 * axis;
        const double rb = snap(1, 5), hb = snap(4, 16);
        const Operand b = cylinder(Frame3(base, axis, trial % 3 == 0 ? Vec3(0, 1, 0) : Vec3(1, 0, 0)), rb, hb);
        double common;
        if (trial % 3 == 0) common = crossedCylinderVolume(ra, ha, base.y(), base.z(), rb, base.x(), base.x() + hb);
        else if (trial % 3 == 1) common = crossedCylinderVolume(ra, ha, base.x(), base.z(), rb, base.y(), base.y() + hb);  // simmetria x <-> y
        else common = discDiscArea(Vec2(0, 0), ra, Vec2(base.x(), base.y()), rb) * overlap(0, ha, base.z(), base.z() + hb);
        for (BooleanOperation op : {BooleanOperation::Unite, BooleanOperation::Intersect, BooleanOperation::Subtract}) {
            const std::string problem = stressCase(a, b, op, expectedVolume(kPi * ra * ra * ha, kPi * rb * rb * hb, common, op));
            if (!problem.empty()) reportFailure(__FILE__, __LINE__, "caso " + std::to_string(trial) + ": " + problem);
        }
    }
}

namespace {

// Volume del risultato contro quello atteso dai volumi dei due solidi e della parte comune.
void checkVolumes(const Body &a, const Body &b, double va, double vb, double common) {
    for (BooleanOperation op : {BooleanOperation::Unite, BooleanOperation::Intersect, BooleanOperation::Subtract}) {
        try {
            const Body result = booleanOperation(a, b, op);
            const double ours = result.faces().empty() ? 0.0 : massProperties(result).volume;
            const double expected = expectedVolume(va, vb, common, op);
            FK_CHECK_NEAR(ours, expected, 1e-9 * std::max(1.0, expected));
        } catch (const std::exception &error) {
            reportFailure(__FILE__, __LINE__, std::string(name(op)) + ": " + error.what());
        }
    }
}

}

// Contatti tangenti, con i volumi esatti (OCCT stesso sbaglia l'unione in
// questi casi): rette di tangenza, punti di contatto isolati, rami che si
// incrociano (cilindri di raggio uguale), anse a otto (un cilindro che tocca
// l'interno dell'altro in un punto), solidi che si toccano lungo uno spigolo.
FK_TEST(BooleanTangentContacts) {
    // Due cilindri di raggio uguale con assi incidenti: volume di Steinmetz 16 r^3 / 3.
    {
        const double r = 5, h = 20;
        const Body a = makeCylinder(Frame3(), r, h), b = makeCylinder(Frame3(Vec3(-10, 0, 10), Vec3(1, 0, 0), Vec3(0, 1, 0)), r, h);
        checkVolumes(a, b, kPi * r * r * h, kPi * r * r * h, 16.0 * r * r * r / 3.0);
    }
    // Cilindro appoggiato sopra un blocco (retta di tangenza, fuori) e dentro
    // un blocco contro una parete (retta di tangenza, dentro).
    {
        const Body block = makeBox(Frame3(), 20, 10, 6);
        const double vBlock = 20 * 10 * 6, vCyl = kPi * 4 * 14;
        checkVolumes(block, makeCylinder(Frame3(Vec3(-2, 5, 8), Vec3(1, 0, 0), Vec3(0, 1, 0)), 2, 14), vBlock, vCyl, 0.0);
        checkVolumes(block, makeCylinder(Frame3(Vec3(3, 5, 4), Vec3(1, 0, 0), Vec3(0, 1, 0)), 2, 14), vBlock, vCyl, vCyl);
    }
    // Cilindri paralleli tangenti dentro e fuori.
    {
        const Body a = makeCylinder(Frame3(), 5, 7);
        checkVolumes(a, makeCylinder(Frame3(Vec3(2, 0, -2), Vec3(0, 0, 1), Vec3(1, 0, 0)), 3, 7), kPi * 25 * 7, kPi * 9 * 7, kPi * 9 * 5);
        checkVolumes(a, makeCylinder(Frame3(Vec3(8, 0, 2), Vec3(0, 0, 1), Vec3(1, 0, 0)), 3, 7), kPi * 25 * 7, kPi * 9 * 7, 0.0);
    }
    // Assi perpendicolari: contatto isolato in un punto (fuori) e ansa a otto (dentro).
    {
        const Body a = makeCylinder(Frame3(), 4, 8);
        checkVolumes(a, makeCylinder(Frame3(Vec3(-10, 6, 4), Vec3(1, 0, 0), Vec3(0, 1, 0)), 2, 20), kPi * 16 * 8, kPi * 4 * 20, 0.0);
        checkVolumes(a, makeCylinder(Frame3(Vec3(-10, -2, 3), Vec3(1, 0, 0), Vec3(0, 1, 0)), 2, 11), kPi * 16 * 8, kPi * 4 * 11,
                     crossedCylinderVolume(4, 8, -2, 3, 2, -10, 1));
    }
    // Blocchi che si toccano lungo uno spigolo; profilo con raccordi il cui
    // lato piatto e' complanare con la faccia dell'altro solido (il piano e'
    // tangente al raccordo lungo lo spigolo).
    {
        const Body a = makeBox(Frame3(), 7, 4, 8);
        checkVolumes(a, makeBox(Frame3(Vec3(7, 4, 0), Vec3(0, 0, 1), Vec3(1, 0, 0)), 4, 6, 10), 7 * 4 * 8, 4 * 6 * 10, 0.0);
        const ProfileRegion region = buildProfile(roundedRectangle(Vec2(0, 0), 20.0, 12.0, 3.0), 1e-6).regions.front();
        const Body rounded = makeExtrusion(Frame3(), region, 5.0);
        const double va = (20 * 12 - (4 - kPi) * 9) * 5;
        checkVolumes(rounded, makeBox(Frame3(Vec3(14, 12, -1), Vec3(0, 0, 1), Vec3(1, 0, 0)), 10, 3, 7), va, 10 * 3 * 7, 0.0);
        // Stesso blocco abbassato: taglia il raccordo; parte comune per integrazione.
        const double y0 = 10.0;
        auto top = [](double x) { return x <= 17.0 ? 12.0 : 9.0 + std::sqrt(std::max(0.0, 9.0 - (x - 17.0) * (x - 17.0))); };
        const double common = integratePieces([&](double x) { return std::max(0.0, top(x) - y0); }, 14.0, 20.0, {17.0}) * 5.0;
        checkVolumes(rounded, makeBox(Frame3(Vec3(14, y0, -1), Vec3(0, 0, 1), Vec3(1, 0, 0)), 10, 5, 7), va, 10 * 5 * 7, common);
    }
}

// Cilindri in posizione generica contro cilindri: nessuna eccezione ammessa.
FK_TEST(BooleanStressCylinders) {
    std::mt19937 rng(779);
    for (int trial = 0; trial < 12; ++trial) {
        const Frame3 frame = randomFrame(rng, 10.0);
        const Operand a = cylinder(frame, uniform(rng, 3, 8), uniform(rng, 8, 15));
        const Vec3 center = frame.toGlobal(Vec3(uniform(rng, -2, 2), uniform(rng, -2, 2), uniform(rng, 2, 8)));
        const Vec3 axis = randomDirection(rng);
        const Operand b = cylinder(Frame3(center - 10.0 * axis, axis, randomDirection(rng)), uniform(rng, 1, 5), uniform(rng, 12, 20));
        for (BooleanOperation op : {BooleanOperation::Unite, BooleanOperation::Intersect, BooleanOperation::Subtract}) {
            try {
                const Body result = booleanOperation(a.body, b.body, op);
                GProp_GProps volume;
                BRepGProp::VolumeProperties(occtBoolean(a.shape, b.shape, op), volume, 1e-12);
                const double ours = result.faces().empty() ? 0.0 : massProperties(result).volume;
                if (std::fabs(ours - volume.Mass()) > 1e-6 * std::max(1.0, volume.Mass()))
                    reportFailure(__FILE__, __LINE__, "caso " + std::to_string(trial) + ": volume " + std::to_string(ours) + " invece di " +
                                                          std::to_string(volume.Mass()));
            } catch (const std::exception &e) {
                reportFailure(__FILE__, __LINE__, "caso " + std::to_string(trial) + ": " + e.what());
            }
        }
    }
}

// Posizioni generiche (rotazioni casuali): nessuna eccezione ammessa.
FK_TEST(BooleanStressGeneral) {
    std::mt19937 rng(778);
    for (int trial = 0; trial < 40; ++trial) {
        const Frame3 frame = randomFrame(rng, 10.0);
        const Operand a = trial % 2 ? box(frame, uniform(rng, 5, 15), uniform(rng, 5, 15), uniform(rng, 5, 15))
                                    : cylinder(frame, uniform(rng, 3, 8), uniform(rng, 5, 15));
        const Operand b = box(rotatedFrame(rng, frame.toGlobal(Vec3(uniform(rng, -3, 5), uniform(rng, -3, 5), uniform(rng, 0, 8)))),
                              uniform(rng, 3, 12), uniform(rng, 3, 12), uniform(rng, 3, 12));
        for (BooleanOperation op : {BooleanOperation::Unite, BooleanOperation::Intersect, BooleanOperation::Subtract}) {
            try {
                const Body result = booleanOperation(a.body, b.body, op);
                GProp_GProps volume;
                BRepGProp::VolumeProperties(occtBoolean(a.shape, b.shape, op), volume, 1e-12);
                const double ours = result.faces().empty() ? 0.0 : massProperties(result).volume;
                if (std::fabs(ours - volume.Mass()) > 1e-7 * std::max(1.0, volume.Mass()))
                    reportFailure(__FILE__, __LINE__, "caso " + std::to_string(trial) + ": volume " + std::to_string(ours) + " invece di " +
                                                          std::to_string(volume.Mass()));
            } catch (const std::exception &e) {
                reportFailure(__FILE__, __LINE__, "caso " + std::to_string(trial) + ": " + e.what());
            }
        }
    }
}

// Contatti lungo una curva: una sfera in un cilindro dello stesso raggio
// (tangenti lungo l'equatore) e un toro attorno a un cilindro che ne tocca
// l'equatore interno. Volumi esatti.
FK_TEST(BooleanTangentCurves) {
    const Frame3 frame(Vec3(1, -2, 3), Vec3(0.2, 0.1, 1), Vec3(1, 0, 0));
    const double r = 4.0;
    const Body sphere = makeRevolution(frame, buildProfile({arcSegment(Vec2(0, 0), r, -kHalfPi, kHalfPi), lineSegment(Vec2(0, r), Vec2(0, -r))}, 1e-9)
                                                  .regions.front());
    const Body cylinder = makeCylinder(frame, r, 10.0);
    checkVolumes(sphere, cylinder, 4.0 * kPi * r * r * r / 3.0, kPi * r * r * 10.0, 2.0 * kPi * r * r * r / 3.0);
    const double big = 5.0, small = 1.5;
    const Body torus = makeRevolution(frame, buildProfile({arcSegment(Vec2(big, 0), small, 0.0, kTwoPi)}, 1e-9).regions.front());
    const Body core = makeCylinder(Frame3(frame.origin() - 3.0 * frame.zDir(), frame.zDir(), frame.xDir()), big - small, 6.0);
    checkVolumes(torus, core, 2.0 * kPi * kPi * big * small * small, kPi * (big - small) * (big - small) * 6.0, 0.0);
}

// Curve d'intersezione che passano per i poli della sfera e per il vertice
// del cono: piani per l'asse (sezioni a meta'), piani inclinati per il polo e
// per il vertice, un cilindro che contiene l'asse (due anse, ciascuna per un
// polo: qui BRepGProp sbaglia di 2e-6 e il riferimento e' l'integrale esatto),
// una rivoluzione di spline che si chiude sull'asse (a meta' per simmetria).
FK_TEST(BooleanThroughPoles) {
    const Frame3 frame(Vec3(1, -2, 3), Vec3(0.2, 0.1, 1), Vec3(1, 0, 0));
    auto local = [&](double x, double y, double z) { return frame.toGlobal(Vec3(x, y, z)); };
    const double r = 4.0;
    const Operand sphere = revolved(frame, {arcSegment(Vec2(0, 0), r, -kHalfPi, kHalfPi), lineSegment(Vec2(0, r), Vec2(0, -r))});
    const Operand half = box(Frame3(local(0, -6, -6), frame.zDir(), frame.xDir()), 10, 12, 12);
    compareAll(sphere, half);
    const Operand cone = revolved(frame, {lineSegment(Vec2(0, -3), Vec2(4, -3)), lineSegment(Vec2(4, -3), Vec2(0, 5)), lineSegment(Vec2(0, 5), Vec2(0, -3))});
    compareAll(cone, half);
    // Piani inclinati per il polo nord e per il vertice del cono.
    auto tiltedAt = [&](const Vec3 &point) {
        const Vec3 n = normalized(frame.toGlobal(Vec3(0.3, 0.2, 1.0)) - frame.origin());
        const Frame3 plane(point, n, frame.xDir());
        return box(Frame3(point - 7.0 * plane.xDir() - 7.0 * plane.yDir() - 12.0 * n, n, plane.xDir()), 14, 14, 12);
    };
    compareAll(sphere, tiltedAt(local(0, 0, r)));
    compareAll(cone, tiltedAt(local(0, 0, 5)));
    // Cilindro che contiene l'asse della sfera: V comune = 2/3 \int (R^3 - (R^2 - 4 a^2 cos^2)^(3/2)).
    const double a = 1.5, h = 16.0;
    const Body through = makeCylinder(Frame3(local(a, 0, -8), frame.zDir(), frame.xDir()), a, h);
    const double common = integratePieces([&](double t) { return 2.0 * (r * r * r - std::pow(r * r - 4 * a * a * std::cos(t) * std::cos(t), 1.5)) / 3.0; },
                                          -kHalfPi, kHalfPi, {0.0});
    checkVolumes(sphere.body, through, 4.0 * kPi * r * r * r / 3.0, kPi * a * a * h, common);
    // Cupola: spline dal bordo all'asse, tagliata a meta' da un piano per l'asse.
    auto spline = std::make_shared<BSplineCurve<2>>(3, std::vector<double>{0, 0, 0, 0, 1, 1, 1, 1},
                                                    std::vector<Vec2>{Vec2(3, 0), Vec2(3, 2), Vec2(1.5, 4), Vec2(0, 4)});
    const Body dome = makeRevolution(frame, buildProfile({lineSegment(Vec2(0, 0), Vec2(3, 0)), ProfileSegment{spline, spline->domain()},
                                                          lineSegment(Vec2(0, 4), Vec2(0, 0))}, 1e-9).regions.front());
    const double vDome = massProperties(dome).volume;
    checkVolumes(dome, half.body, vDome, 10 * 12 * 12, 0.5 * vDome);
    // Cilindro la cui superficie passa per il vertice del cono (curva
    // tracciata per il vertice). V comune = \int 8 (1 - rho / 4) sul disco rho <= 3 cos t.
    const Body cylinderThroughApex = makeCylinder(Frame3(local(1.5, 0, -8), frame.zDir(), frame.xDir()), 1.5, 16.0);
    checkVolumes(cone.body, cylinderThroughApex, kPi * 16.0 * 8.0 / 3.0, kPi * 2.25 * 16.0, 8.0 * (4.5 * kHalfPi - 3.0));
}

// Lamine contro solidi: la parte della lamina dentro il solido
// (intersezione, anche con il solido per primo) e quella fuori (differenza).
// Riferimento: aree esatte. Unione, lamina tolta da un solido e due lamine: errore.
FK_TEST(BooleanSheets) {
    const Frame3 frame(Vec3(1, -2, 3), Vec3(0.2, 0.1, 1), Vec3(1, 0, 0));
    auto local = [&](double x, double y, double z) { return frame.toGlobal(Vec3(x, y, z)); };
    auto sheetOf = [&](const std::vector<ProfileSegment> &segments, double height) {
        return makeSheetExtrusion(frame, buildProfile(segments, 1e-9).chains, height);
    };
    auto area = [](const Body &body) {
        double total = 0.0;
        for (FaceId f : body.faces()) total += faceArea(body, f);
        return total;
    };
    auto check = [&](const Body &sheet, const Body &solid, double inside) {
        const double total = area(sheet);
        try {
            const Body in = booleanOperation(sheet, solid, BooleanOperation::Intersect);
            const Body swapped = booleanOperation(solid, sheet, BooleanOperation::Intersect);
            const Body out = booleanOperation(sheet, solid, BooleanOperation::Subtract);
            FK_CHECK(in.isSheet() && swapped.isSheet() && out.isSheet());
            FK_CHECK_NEAR(area(in), inside, 1e-9 * total);
            FK_CHECK_NEAR(area(swapped), inside, 1e-9 * total);
            FK_CHECK_NEAR(area(out), total - inside, 1e-9 * total);
        } catch (const std::exception &error) {
            reportFailure(__FILE__, __LINE__, error.what());
        }
    };
    // Lamina piana (segmento estruso) per meta' dentro un blocco; poi a L.
    const Body block = makeBox(Frame3(local(0, -5, -1), frame.zDir(), frame.xDir()), 10, 10, 10);
    check(sheetOf({lineSegment(Vec2(-6, 0), Vec2(6, 0))}, 6.0), block, 6.0 * 6.0);
    check(sheetOf({lineSegment(Vec2(-6, 1), Vec2(3, 1)), lineSegment(Vec2(3, 1), Vec2(3, 8))}, 4.0), block, 3.0 * 4.0 + 4.0 * 4.0);
    // Mezzo cilindro (arco estruso) contro un blocco e contro un cilindro coassiale piu' corto.
    const double r = 3.0;
    const Body shell = sheetOf({arcSegment(Vec2(0, 0), r, 0.0, kPi)}, 8.0);
    check(shell, block, r * kHalfPi * 8.0);
    check(shell, makeCylinder(Frame3(local(0, 0, 2), frame.zDir(), frame.xDir()), 4.0, 3.0), r * kPi * 3.0);
    // Cilindro dello stesso raggio: la lamina sta sulla sua superficie (bordo: resta nell'intersezione).
    check(shell, makeCylinder(Frame3(local(0, 0, 2), frame.zDir(), frame.xDir()), r, 3.0), r * kPi * 3.0);
    FK_CHECK_THROWS(booleanOperation(shell, block, BooleanOperation::Unite));
    FK_CHECK_THROWS(booleanOperation(block, shell, BooleanOperation::Subtract));
    FK_CHECK_THROWS(booleanOperation(shell, shell, BooleanOperation::Intersect));
}

// Fianchi estrusi che coincidono solo in parte: B usa un pezzo della spline
// di A (curva diversa: de Casteljau), anche in un tratto che A non ha.
// Volumi esatti: aree dei profili (integrali esatti sulle curve) per le altezze.
FK_TEST(BooleanPartialCoincidence) {
    const Frame3 frame(Vec3(1, -2, 3), Vec3(0.2, 0.1, 1), Vec3(1, 0, 0));
    auto at = [&](double z) { return Frame3(frame.toGlobal(Vec3(0, 0, z)), frame.zDir(), frame.xDir()); };
    const std::vector<Vec2> poles{Vec2(0, 0), Vec2(2, 5), Vec2(8, 6), Vec2(10, 0)};
    // Pezzo [a, b] della cubica di Bezier, riparametrizzato su [0, 1].
    auto piece = [&](double a, double b) {
        auto split = [](std::vector<Vec2> p, double t, bool left) {
            std::vector<Vec2> l{p[0]}, r{p[3]};
            for (int level = 1; level < 4; ++level) {
                for (int i = 0; i + level < 4; ++i) p[i] = (1 - t) * p[i] + t * p[i + 1];
                l.push_back(p[0]);
                r.insert(r.begin(), p[3 - level]);
            }
            return left ? l : r;
        };
        std::vector<Vec2> q = split(poles, b, true);
        q = split(q, a / b, false);
        auto curve = std::make_shared<BSplineCurve<2>>(3, std::vector<double>{0, 0, 0, 0, 1, 1, 1, 1}, q);
        return ProfileSegment{curve, curve->domain()};
    };
    auto whole = std::make_shared<BSplineCurve<2>>(3, std::vector<double>{0, 0, 0, 0, 1, 1, 1, 1}, poles);
    const ProfileSegment arc{whole, whole->domain()};
    auto region = [](const std::vector<ProfileSegment> &segments) { return buildProfile(segments, 1e-9).regions.front(); };
    const ProfileRegion ra = region({arc, lineSegment(Vec2(10, 0), Vec2(0, 0))});
    const Body a = makeExtrusion(at(0), ra, 6.0);
    // B: il pezzo [0.3, 0.8] con la sua corda, piu' alto e spostato (dentro A).
    const ProfileSegment middle = piece(0.3, 0.8);
    const ProfileRegion rb = region({middle, lineSegment(middle.curve->point(1.0), middle.curve->point(0.0))});
    const Body b = makeExtrusion(at(2), rb, 8.0);
    checkVolumes(a, b, area(ra) * 6.0, area(rb) * 8.0, area(rb) * 4.0);
    // C: il pezzo [0.5, 1] e poi fuori da A (un triangolo sotto la base di A).
    const ProfileSegment tail = piece(0.5, 1.0);
    const Vec2 start = tail.curve->point(0.0);
    const ProfileRegion rc = region({tail, lineSegment(Vec2(10, 0), Vec2(12, -3)), lineSegment(Vec2(12, -3), start)});
    const Body c = makeExtrusion(at(-1), rc, 4.0);
    // Parte comune: la regione di C sopra la base di A (y >= 0), fino all'altezza 3.
    const double lineX = start.x() + (Vec2(12, -3) - start).x() * (start.y() / (start.y() + 3.0));
    const ProfileRegion rcAbove = region({tail, lineSegment(Vec2(10, 0), Vec2(lineX, 0)), lineSegment(Vec2(lineX, 0), start)});
    checkVolumes(a, c, area(ra) * 6.0, area(rc) * 4.0, area(rcAbove) * 3.0);
    // D: una spline che segue quella di A in [0.3, 0.6] e poi se ne stacca
    // tangente, dentro A: le superfici si separano lungo una generatrice
    // interna alla faccia di D.
    const ProfileSegment first = piece(0.3, 0.6);
    std::vector<Vec2> q = static_cast<const BSplineCurve<2> &>(*first.curve).poles();
    q.push_back(q[3] + (q[3] - q[2]));
    q.push_back(Vec2(7, 2.5));
    q.push_back(Vec2(6, 1.5));
    auto departing = std::make_shared<BSplineCurve<2>>(3, std::vector<double>{0, 0, 0, 0, 1, 1, 1, 2, 2, 2, 2}, q);
    const ProfileRegion rd = region({ProfileSegment{departing, departing->domain()}, lineSegment(Vec2(6, 1.5), q.front())});
    const Body d = makeExtrusion(at(1), rd, 3.0);
    checkVolumes(a, d, area(ra) * 6.0, area(rd) * 3.0, area(rd) * 3.0);
}

// Piano bitangente al toro (per il centro, inclinato di asin(r / R)): la
// sezione sono i due cerchi di Villarceau, che si incrociano nei due punti di
// tangenza e si avvolgono nelle due direzioni del toro. Per simmetria ogni
// meta' ha volume pari a meta' del toro.
FK_TEST(BooleanVillarceau) {
    const Frame3 frame(Vec3(1, -2, 3), Vec3(0.2, 0.1, 1), Vec3(1, 0, 0));
    const double big = 5.0, small = 1.5, tilt = std::asin(small / big);
    const Body torus = makeRevolution(frame, buildProfile({arcSegment(Vec2(big, 0), small, 0.0, kTwoPi)}, 1e-9).regions.front());
    const Vec3 n = normalized(frame.toGlobal(Vec3(0, -std::sin(tilt), std::cos(tilt))) - frame.origin());
    const Frame3 plane(frame.origin(), n, frame.xDir());
    const Body block = makeBox(Frame3(frame.origin() - 10.0 * plane.xDir() - 10.0 * plane.yDir(), n, plane.xDir()), 20, 20, 10);
    const double volume = 2.0 * kPi * kPi * big * small * small;
    checkVolumes(torus, block, volume, 20 * 20 * 10, 0.5 * volume);
}

// Contatti tangenti di ordine superiore in un punto: una cupola (spline con
// curvatura nulla sull'asse) che tocca nel polo un blocco appoggiato sopra, o
// la faccia superiore di un blocco che la contiene. Nessuna curva: il
// contatto non divide le facce.
FK_TEST(BooleanHigherOrderContacts) {
    const Frame3 frame(Vec3(1, -2, 3), Vec3(0.2, 0.1, 1), Vec3(1, 0, 0));
    auto local = [&](double x, double y, double z) { return frame.toGlobal(Vec3(x, y, z)); };
    auto spline = std::make_shared<BSplineCurve<2>>(3, std::vector<double>{0, 0, 0, 0, 1, 1, 1, 1},
                                                    std::vector<Vec2>{Vec2(3, 0), Vec2(3, 4), Vec2(2, 4), Vec2(0, 4)});
    const Body dome = makeRevolution(frame, buildProfile({lineSegment(Vec2(0, 0), Vec2(3, 0)), ProfileSegment{spline, spline->domain()},
                                                          lineSegment(Vec2(0, 4), Vec2(0, 0))}, 1e-9).regions.front());
    const double vDome = massProperties(dome).volume;
    const Body above = makeBox(Frame3(local(-5, -5, 4), frame.zDir(), frame.xDir()), 10, 10, 3);
    checkVolumes(dome, above, vDome, 300, 0.0);
    // Blocco che contiene la cupola e ne tocca il polo con la faccia superiore.
    const Body around = makeBox(Frame3(local(-5, -5, -1), frame.zDir(), frame.xDir()), 10, 10, 5);
    checkVolumes(dome, around, vDome, 500, vDome);
    // Cilindro orizzontale tangente nel polo: ne partono rami tangenti tra
    // loro che il tracciamento non segue fino al punto. Errore esplicito,
    // non un risultato sbagliato.
    const Body roller = makeCylinder(Frame3(local(-6, 0, -6), frame.xDir(), frame.yDir()), 10.0, 12.0);
    FK_CHECK_THROWS(booleanOperation(dome, roller, BooleanOperation::Intersect));
}

// Superfici di rivoluzione coassiali che coincidono in parte: una cupola di
// spline e corpi rivoltati attorno allo stesso asse con un pezzo della stessa
// spline (curva diversa: de Casteljau), dentro e fuori dalla cupola; poi una
// sfera e un cilindro coassiali con la cupola (cerchi esatti dai profili).
FK_TEST(BooleanCoaxialRevolutions) {
    const Frame3 frame(Vec3(1, -2, 3), Vec3(0.2, 0.1, 1), Vec3(1, 0, 0));
    const std::vector<Vec2> poles{Vec2(3, 0), Vec2(3, 2), Vec2(1.5, 4), Vec2(0, 4)};
    auto split = [](std::vector<Vec2> p, double t, bool left) {
        std::vector<Vec2> l{p[0]}, r{p[3]};
        for (int level = 1; level < 4; ++level) {
            for (int i = 0; i + level < 4; ++i) p[i] = (1 - t) * p[i] + t * p[i + 1];
            l.push_back(p[0]);
            r.insert(r.begin(), p[3 - level]);
        }
        return left ? l : r;
    };
    auto piece = [&](double a, double b) {
        std::vector<Vec2> q = split(poles, b, true);
        q = split(q, a / b, false);
        auto curve = std::make_shared<BSplineCurve<2>>(3, std::vector<double>{0, 0, 0, 0, 1, 1, 1, 1}, q);
        return ProfileSegment{curve, curve->domain()};
    };
    auto revolve = [&](const std::vector<ProfileSegment> &segments) { return makeRevolution(frame, buildProfile(segments, 1e-9).regions.front()); };
    auto whole = std::make_shared<BSplineCurve<2>>(3, std::vector<double>{0, 0, 0, 0, 1, 1, 1, 1}, poles);
    const Body dome = revolve({lineSegment(Vec2(0, 0), Vec2(3, 0)), ProfileSegment{whole, whole->domain()}, lineSegment(Vec2(0, 4), Vec2(0, 0))});
    const double vDome = massProperties(dome).volume;
    // Dentro: il pezzo [0.2, 0.7] chiuso verso l'asse.
    const ProfileSegment inner = piece(0.2, 0.7);
    const Vec2 p0 = inner.curve->point(0.0), p1 = inner.curve->point(1.0);
    const Body core = revolve({inner, lineSegment(p1, Vec2(0, p1.y())), lineSegment(Vec2(0, p1.y()), Vec2(0, p0.y())), lineSegment(Vec2(0, p0.y()), p0)});
    const double vCore = massProperties(core).volume;
    checkVolumes(dome, core, vDome, vCore, vCore);
    // Fuori: il pezzo [0.3, 0.8] con un anello verso l'esterno (solo la striscia in comune).
    const ProfileSegment outer = piece(0.3, 0.8);
    const Vec2 q0 = outer.curve->point(0.0), q1 = outer.curve->point(1.0);
    const Body shell = revolve({outer, lineSegment(q1, q1 + Vec2(2, 0)), lineSegment(q1 + Vec2(2, 0), q0 + Vec2(2, 0)), lineSegment(q0 + Vec2(2, 0), q0)});
    checkVolumes(dome, shell, vDome, massProperties(shell).volume, 0.0);
    // Sfera e cilindro coassiali (cerchi dai profili, niente tracciamento).
    const Body ball = revolve({arcSegment(Vec2(0, 2), 2.5, -kHalfPi, kHalfPi), lineSegment(Vec2(0, 4.5), Vec2(0, -0.5))});
    const Body post = makeCylinder(Frame3(frame.toGlobal(Vec3(0, 0, -1)), frame.zDir(), frame.xDir()), 1.2, 7.0);
    // Parte comune con il cilindro: \int 2 pi rho z(rho) drho per rho < 1.2 (sopra la base z = 0, fino a z = 6).
    {
        double lo = 0.0, hi = 1.0;
        for (int i = 0; i < 100; ++i) {
            const double m = 0.5 * (lo + hi);
            (whole->point(m).x() > 1.2 ? lo : hi) = m;
        }
        const double common = ForgeCad::Kernel::detail::integrate(
            [&](double t) {
                Vec2 d[2];
                whole->evaluate(t, 1, d);
                return -kTwoPi * d[0].x() * d[0].y() * d[1].x();
            },
            lo, 1.0, 1e-13);
        checkVolumes(dome, post, vDome, kPi * 1.44 * 7.0, common);
    }
    for (const Body *other : {&ball, &post}) {
        try {
            const double vi = massProperties(booleanOperation(dome, *other, BooleanOperation::Intersect)).volume;
            const double vu = massProperties(booleanOperation(dome, *other, BooleanOperation::Unite)).volume;
            const double vd = massProperties(booleanOperation(dome, *other, BooleanOperation::Subtract)).volume;
            FK_CHECK_NEAR(vu, vDome + massProperties(*other).volume - vi, 1e-9 * vu);
            FK_CHECK_NEAR(vd, vDome - vi, 1e-9 * vu);
        } catch (const std::exception &e) {
            reportFailure(__FILE__, __LINE__, e.what());
        }
    }
}
