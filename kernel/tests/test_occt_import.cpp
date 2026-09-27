#include <BRepAdaptor_Curve.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepGProp.hxx>
#include <BRepOffsetAPI_ThruSections.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepPrimAPI_MakeRevol.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <BRepPrimAPI_MakeTorus.hxx>
#include <GProp_GProps.hxx>
#include <GeomAPI_Interpolate.hxx>
#include <Geom_BSplineCurve.hxx>
#include <Geom_CylindricalSurface.hxx>
#include <Geom_Line.hxx>
#include <Geom_SurfaceOfLinearExtrusion.hxx>
#include <Geom_SurfaceOfRevolution.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <STEPControl_Reader.hxx>
#include <STEPControl_Writer.hxx>
#include <TColgp_HArray1OfPnt.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <gp_Ax2.hxx>
#include <gp_Ax3.hxx>
#include <gp_Circ.hxx>
#include <gp_Trsf.hxx>

#include <cmath>
#include <cstdio>

#include "fk_body_check.h"
#include "fk_mass.h"
#include "fk_occt_import.h"
#include "fk_tessellate.h"
#include "fk_test.h"
#include "fk_test_util.h"

using namespace fktest;

namespace {

double occtVolume(const TopoDS_Shape &shape) {
    GProp_GProps props;
    BRepGProp::VolumeProperties(shape, props, 1e-10);
    return props.Mass();
}

// Converte, controlla e tassella: le proprieta' di massa del corpo convertito.
MassProperties converted(const TopoDS_Shape &shape, OcctImportReport *report = nullptr) {
    const Body body = bodyFromOcct(shape, report);
    for (const CheckIssue &issue : checkBody(body)) reportFailure(__FILE__, __LINE__, describe(issue.code) + ": " + issue.message);
    TessellationOptions options;
    options.deflection = 0.01;
    FK_CHECK(tessellate(body, options).failedFaces == 0);
    return massProperties(body, 1e-12);
}

TopoDS_Face splineFace() {
    Handle(TColgp_HArray1OfPnt) points = new TColgp_HArray1OfPnt(1, 5);
    const double xy[5][2] = {{0, 0}, {1, 0.4}, {2, -0.3}, {3, 0.5}, {4, 0}};
    for (int i = 0; i < 5; ++i) points->SetValue(i + 1, gp_Pnt(xy[i][0], xy[i][1] + 1.0, 0.0));
    GeomAPI_Interpolate interpolate(points, false, 1e-9);
    interpolate.Perform();
    const TopoDS_Edge spline = BRepBuilderAPI_MakeEdge(interpolate.Curve()).Edge();
    const TopoDS_Edge right = BRepBuilderAPI_MakeEdge(gp_Pnt(4, 1, 0), gp_Pnt(4, 3, 0)).Edge();
    const TopoDS_Edge top = BRepBuilderAPI_MakeEdge(gp_Pnt(4, 3, 0), gp_Pnt(0, 3, 0)).Edge();
    const TopoDS_Edge left = BRepBuilderAPI_MakeEdge(gp_Pnt(0, 3, 0), gp_Pnt(0, 1, 0)).Edge();
    return BRepBuilderAPI_MakeFace(BRepBuilderAPI_MakeWire(spline, right, top, left).Wire()).Face();
}

}

FK_TEST(OcctImportPrimitives) {
    // Volumi esatti: cuciture (cilindro, cono, toro) e poli (sfera, vertice del cono) tolti.
    FK_CHECK_NEAR(converted(BRepPrimAPI_MakeBox(gp_Pnt(1, 2, 3), 2.0, 3.0, 1.5).Shape()).volume, 9.0, 1e-11);
    const gp_Ax2 axis(gp_Pnt(0.5, -1, 2), gp_Dir(1, 1, 0.3));
    FK_CHECK_NEAR(converted(BRepPrimAPI_MakeCylinder(axis, 1.5, 4.0).Shape()).volume, kPi * 1.5 * 1.5 * 4.0, 1e-10);
    FK_CHECK_NEAR(converted(BRepPrimAPI_MakeCylinder(axis, 1.5, 4.0, 2.0).Shape()).volume, kPi * 1.5 * 1.5 * 4.0 / kPi, 1e-10);
    OcctImportReport report;
    FK_CHECK_NEAR(converted(BRepPrimAPI_MakeSphere(gp_Pnt(1, 1, 1), 2.0).Shape(), &report).volume, 4.0 / 3.0 * kPi * 8.0, 1e-9);
    FK_CHECK_NEAR(report.droppedSeams, 2, 0);  // la cucitura compare due volte nel loop
    FK_CHECK_NEAR(report.droppedDegenerated, 2, 0);
    FK_CHECK_NEAR(converted(BRepPrimAPI_MakeSphere(gp_Pnt(0, 0, 0), 2.0, 0.0, kPi / 2).Shape()).volume, 2.0 / 3.0 * kPi * 8.0, 1e-9);
    FK_CHECK_NEAR(converted(BRepPrimAPI_MakeCone(axis, 2.0, 0.0, 3.0).Shape()).volume, kPi * 4.0 * 3.0 / 3.0, 1e-9);
    FK_CHECK_NEAR(converted(BRepPrimAPI_MakeCone(axis, 2.0, 1.0, 3.0).Shape()).volume, kPi * 3.0 / 3.0 * (4.0 + 2.0 + 1.0), 1e-9);
    FK_CHECK_NEAR(converted(BRepPrimAPI_MakeTorus(axis, 3.0, 1.0).Shape()).volume, 2.0 * kPi * kPi * 3.0, 1e-9);
    FK_CHECK_NEAR(converted(BRepPrimAPI_MakeTorus(axis, 3.0, 1.0, kPi / 2).Shape()).volume, kPi * kPi * 3.0 / 2.0, 1e-9);
}

FK_TEST(OcctImportModelled) {
    // Booleana, raccordi, rivoluzione e prisma di una spline: contro BRepGProp.
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(gp_Pnt(-2, -2, 0), 4.0, 4.0, 2.0).Shape();
    const TopoDS_Shape hole = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0.5, 0, -1), gp_Dir(0, 0, 1)), 1.0, 4.0).Shape();
    const TopoDS_Shape cut = BRepAlgoAPI_Cut(box, hole).Shape();
    const double cutVolume = 32.0 - kPi * 2.0;
    FK_CHECK_NEAR(converted(cut).volume, cutVolume, 1e-10 * cutVolume);
    BRepFilletAPI_MakeFillet fillet(cut);
    // Gli spigoli verticali del parallelepipedo e il bordo superiore del foro.
    for (TopExp_Explorer e(cut, TopAbs_EDGE); e.More(); e.Next()) {
        BRepAdaptor_Curve curve(TopoDS::Edge(e.Current()));
        const bool vertical = curve.GetType() == GeomAbs_Line && std::fabs(curve.Line().Direction().Z()) > 0.99;
        const bool rim = curve.GetType() == GeomAbs_Circle && curve.Circle().Location().Z() > 1.99;
        if (vertical || rim) fillet.Add(0.3, TopoDS::Edge(e.Current()));
    }
    fillet.Build();
    FK_CHECK(fillet.IsDone());
    const double filletVolume = occtVolume(fillet.Shape());
    FK_CHECK_NEAR(converted(fillet.Shape()).volume, filletVolume, 1e-6 * filletVolume);
    // Rivoluzione della faccia con la spline attorno all'asse X (Pappus non basta: BRepGProp).
    const TopoDS_Face face = splineFace();
    const TopoDS_Shape revolved = BRepPrimAPI_MakeRevol(face, gp_Ax1(gp_Pnt(0, 0, 0), gp_Dir(1, 0, 0))).Shape();
    const double revolvedVolume = occtVolume(revolved);
    FK_CHECK_NEAR(converted(revolved).volume, revolvedVolume, 1e-6 * revolvedVolume);
    // Prisma: area esatta del profilo per l'altezza.
    GProp_GProps area;
    BRepGProp::SurfaceProperties(face, area, 1e-13);
    const TopoDS_Shape prism = BRepPrimAPI_MakePrism(face, gp_Vec(0, 0, 2.5)).Shape();
    FK_CHECK_NEAR(converted(prism).volume, area.Mass() * 2.5, 1e-9 * area.Mass());
    // Specchio: gli assi dei piani e dei cilindri diventano indiretti.
    gp_Trsf mirror;
    mirror.SetMirror(gp_Ax2(gp_Pnt(0, 0, 0), gp_Dir(1, 0.2, 0)));
    const TopoDS_Shape mirrored = BRepBuilderAPI_Transform(cut, mirror, true).Shape();
    const MassProperties m = converted(mirrored);
    FK_CHECK_NEAR(m.volume, cutVolume, 1e-10 * cutVolume);
}

FK_TEST(OcctImportSplineSurfaces) {
    // Loft liscio tra cerchi: superfici B-spline con la cucitura (resta, con le SP-curve di OCCT).
    BRepOffsetAPI_ThruSections loft(true, false, 1e-7);
    const double radii[3] = {2.0, 1.0, 2.0};
    for (int i = 0; i < 3; ++i) {
        const gp_Circ circle(gp_Ax2(gp_Pnt(0, 0, 2.0 * i), gp_Dir(0, 0, 1)), radii[i]);
        loft.AddWire(BRepBuilderAPI_MakeWire(BRepBuilderAPI_MakeEdge(circle).Edge()).Wire());
    }
    loft.Build();
    FK_CHECK(loft.IsDone());
    const double volume = occtVolume(loft.Shape());
    FK_CHECK_NEAR(converted(loft.Shape()).volume, volume, 1e-6 * volume);
}

FK_TEST(OcctImportSheet) {
    // Lamina su un cilindro con gp_Ax3 indiretto (u -> -u): l'area resta.
    gp_Ax3 axes(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1), gp_Dir(1, 0, 0));
    axes.YReverse();
    FK_CHECK(!axes.Direct());
    const Handle(Geom_CylindricalSurface) cylinder = new Geom_CylindricalSurface(axes, 2.0);
    const TopoDS_Face face = BRepBuilderAPI_MakeFace(cylinder, 0.0, 1.5, 0.0, 3.0, 1e-7).Face();
    const Body body = bodyFromOcct(face);
    FK_CHECK(body.isSheet());
    FK_CHECK(checkBody(body).empty());
    double area = 0.0;
    for (FaceId f : body.faces()) area += faceArea(body, f);
    FK_CHECK_NEAR(area, 2.0 * 1.5 * 3.0, 1e-10);
}

FK_TEST(OcctImportUnboundedBases) {
    // Come negli IGES di superfici: rette illimitate come curve base di rivoluzioni ed estrusioni.
    const auto sheetArea = [](const TopoDS_Face &face) {
        const Body body = bodyFromOcct(face);
        FK_CHECK(checkBody(body).empty());
        double area = 0.0;
        for (FaceId f : body.faces()) area += faceArea(body, f);
        return area;
    };
    const Handle(Geom_Line) parallel = new Geom_Line(gp_Pnt(2, 0, 0), gp_Dir(0, 0, 1));
    const Handle(Geom_SurfaceOfRevolution) cylinder = new Geom_SurfaceOfRevolution(parallel, gp_Ax1(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)));
    FK_CHECK_NEAR(sheetArea(BRepBuilderAPI_MakeFace(cylinder, 0.0, 2.0, -1.0, 3.0, 1e-7).Face()), 2.0 * 2.0 * 4.0, 1e-10);
    const Handle(Geom_Line) slanted = new Geom_Line(gp_Pnt(1, 0, 0), gp_Dir(1, 0, 1));
    const Handle(Geom_SurfaceOfRevolution) cone = new Geom_SurfaceOfRevolution(slanted, gp_Ax1(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)));
    // Tronco di cono da raggio 1 a 1 + sqrt(2)/2 * 2: area laterale pi (r1 + r2) g, g = 2.
    const double r2 = 1.0 + std::sqrt(2.0);
    FK_CHECK_NEAR(sheetArea(BRepBuilderAPI_MakeFace(cone, 0.0, 2 * kPi, 0.0, 2.0, 1e-7).Face()), kPi * (1.0 + r2) * 2.0, 1e-9);
    const Handle(Geom_Line) base = new Geom_Line(gp_Pnt(0, 0, 0), gp_Dir(1, 0, 0));
    const Handle(Geom_SurfaceOfLinearExtrusion) plane = new Geom_SurfaceOfLinearExtrusion(base, gp_Dir(0, 1, 1));
    FK_CHECK_NEAR(sheetArea(BRepBuilderAPI_MakeFace(plane, 0.0, 3.0, 0.0, 2.0, 1e-7).Face()), 3.0 * 2.0, 1e-10);  // direzione unitaria, normale alla retta
}

FK_TEST(OcctImportStepRoundTrip) {
    // Scritto e riletto da STEP (i corpi importati dell'app passano di qui).
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(gp_Pnt(-2, -2, 0), 4.0, 4.0, 2.0).Shape();
    const TopoDS_Shape sphere = BRepPrimAPI_MakeSphere(gp_Pnt(0, 0, 2), 1.2).Shape();
    const TopoDS_Shape shape = BRepAlgoAPI_Cut(box, sphere).Shape();
    const std::string path = std::string(P_tmpdir) + "/forgekernel_occt_import_test.step";
    STEPControl_Writer writer;
    FK_CHECK(writer.Transfer(shape, STEPControl_AsIs) == IFSelect_RetDone);
    FK_CHECK(writer.Write(path.c_str()) == IFSelect_RetDone);
    STEPControl_Reader reader;
    FK_CHECK(reader.ReadFile(path.c_str()) == IFSelect_RetDone);
    reader.TransferRoots();
    const TopoDS_Shape read = reader.OneShape();
    std::remove(path.c_str());
    const double volume = 32.0 - 2.0 / 3.0 * kPi * 1.2 * 1.2 * 1.2;
    FK_CHECK_NEAR(converted(read).volume, volume, 1e-9 * volume);
}
