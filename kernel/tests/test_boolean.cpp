#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <GProp_GProps.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Tool.hxx>
#include <Poly_Triangulation.hxx>
#include <TopExp_Explorer.hxx>

#include "fk_body_check.h"
#include "fk_boolean.h"
#include "fk_extrude.h"
#include "fk_mass.h"
#include "fk_primitives.h"
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
FK_TEST(BooleanUnsupportedCases) {
    const Body a = makeCylinder(Frame3(), 5, 20);
    // Raggi uguali e assi incidenti: le curve si incrociano in punti di tangenza.
    FK_CHECK_THROWS(booleanOperation(a, makeCylinder(Frame3(Vec3(-10, 0, 10), Vec3(1, 0, 0), Vec3(0, 1, 0)), 5, 20),
                                     BooleanOperation::Unite));
    // Stesso cilindro spostato lungo l'asse: fianchi coincidenti.
    FK_CHECK_THROWS(booleanOperation(a, makeCylinder(Frame3(Vec3(0, 0, 10), Vec3(0, 0, 1), Vec3(1, 0, 0)), 5, 20),
                                     BooleanOperation::Unite));
}

namespace {

// Esito di una booleana contro OCCT (solo volume): "" se torna, altrimenti il
// problema. `tangent`: la booleana e' stata rifiutata per un contatto tangente.
std::string stressCase(const Operand &a, const Operand &b, BooleanOperation op, bool &tangent) {
    tangent = false;
    try {
        const Body result = booleanOperation(a.body, b.body, op);
        GProp_GProps volume;
        BRepGProp::VolumeProperties(occtBoolean(a.shape, b.shape, op), volume, 1e-12);
        const double ours = result.faces().empty() ? 0.0 : massProperties(result).volume;
        if (std::fabs(ours - volume.Mass()) > 1e-7 * std::max(1.0, volume.Mass()))
            return "volume " + std::to_string(ours) + " invece di " + std::to_string(volume.Mass());
        return "";
    } catch (const std::exception &e) {
        tangent = std::string(e.what()).find("tangente") != std::string::npos;
        return std::string("eccezione: ") + e.what();
    }
}

}

// Configurazioni degeneri su una griglia intera: facce complanari, spigoli
// e vertici sulle facce dell'altro solido, cilindri tangenti. Nessun
// risultato deve essere sbagliato; i contatti tangenti sono rifiutati
// (OCCT stesso sbaglia l'unione in quei casi) e al piu' qualche caso
// degenere (vertice su vertice) puo' fallire con un'eccezione.
FK_TEST(BooleanStressGrid) {
    std::mt19937 rng(777);
    int wrong = 0, errors = 0, tangents = 0;
    for (int trial = 0; trial < 150; ++trial) {
        auto snap = [&](double lo, double hi) { return std::round(uniform(rng, lo, hi)); };
        const Operand a = box(Frame3(), snap(4, 12), snap(4, 12), snap(4, 12));
        Operand b;
        switch (trial % 3) {
        case 0: {
            const Vec3 corner(snap(-4, 8), snap(-4, 8), snap(-4, 8));
            b = box(Frame3(corner, Vec3(0, 0, 1), Vec3(1, 0, 0)), snap(2, 10), snap(2, 10), snap(2, 10));
            break;
        }
        case 1: {
            const Vec3 base(snap(0, 10), snap(0, 10), snap(-4, 6));
            b = cylinder(Frame3(base, Vec3(0, 0, 1), Vec3(1, 0, 0)), snap(1, 4), snap(2, 12));
            break;
        }
        default: {
            const Vec3 base(snap(-4, 2), snap(0, 10), snap(0, 10));
            b = cylinder(Frame3(base, Vec3(1, 0, 0), Vec3(0, 1, 0)), snap(1, 4), snap(4, 16));
            break;
        }
        }
        for (BooleanOperation op : {BooleanOperation::Unite, BooleanOperation::Intersect, BooleanOperation::Subtract}) {
            bool tangent;
            const std::string problem = stressCase(a, b, op, tangent);
            if (problem.empty()) continue;
            if (tangent) ++tangents;
            else if (problem.rfind("eccezione", 0) == 0) ++errors;
            else {
                ++wrong;
                reportFailure(__FILE__, __LINE__, "caso " + std::to_string(trial) + ": " + problem);
            }
        }
    }
    FK_CHECK(wrong == 0);
    FK_CHECK(errors <= 3);
    FK_CHECK(tangents < 150);
}

// Cilindri su una griglia intera, assi paralleli o perpendicolari: raggi
// uguali, fianchi tangenti o coincidenti, cerchi sulle facce dell'altro.
// Nessun risultato sbagliato; i casi non gestiti danno un'eccezione.
FK_TEST(BooleanStressCylinderGrid) {
    std::mt19937 rng(780);
    int wrong = 0, errors = 0, tangents = 0;
    for (int trial = 0; trial < 60; ++trial) {
        auto snap = [&](double lo, double hi) { return std::round(uniform(rng, lo, hi)); };
        const double ra = snap(2, 5), ha = snap(4, 10);
        const Operand a = cylinder(Frame3(Vec3(0, 0, 0), Vec3(0, 0, 1), Vec3(1, 0, 0)), ra, ha);
        const Vec3 axes[3] = {Vec3(1, 0, 0), Vec3(0, 1, 0), Vec3(0, 0, 1)};
        const Vec3 axis = axes[trial % 3];
        const Vec3 base = trial % 3 == 2 ? Vec3(snap(-4, 4), snap(-4, 4), snap(-4, 8)) : Vec3(snap(-4, 4), snap(-4, 4), snap(0, 10)) - 8.0 * axis;
        const double rb = snap(1, 5), hb = snap(4, 16);
        const Operand b = cylinder(Frame3(base, axis, trial % 3 == 0 ? Vec3(0, 1, 0) : Vec3(1, 0, 0)), rb, hb);
        for (BooleanOperation op : {BooleanOperation::Unite, BooleanOperation::Intersect, BooleanOperation::Subtract}) {
            try {
                const Body result = booleanOperation(a.body, b.body, op);
                GProp_GProps volume;
                BRepGProp::VolumeProperties(occtBoolean(a.shape, b.shape, op), volume, 1e-12);
                const double ours = result.faces().empty() ? 0.0 : massProperties(result).volume;
                if (std::fabs(ours - volume.Mass()) > 1e-6 * std::max(1.0, volume.Mass())) {
                    ++wrong;
                    reportFailure(__FILE__, __LINE__, "caso " + std::to_string(trial) + ": volume " + std::to_string(ours) + " invece di " +
                                                          std::to_string(volume.Mass()));
                }
            } catch (const std::exception &e) {
                if (std::string(e.what()).find("tangente") != std::string::npos) ++tangents;
                else ++errors;
            }
        }
    }
    FK_CHECK(wrong == 0);
    FK_CHECK(errors <= 3);
    FK_CHECK(tangents < 90);
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
            bool tangent;
            const std::string problem = stressCase(a, b, op, tangent);
            if (!problem.empty()) reportFailure(__FILE__, __LINE__, "caso " + std::to_string(trial) + ": " + problem);
        }
    }
}
