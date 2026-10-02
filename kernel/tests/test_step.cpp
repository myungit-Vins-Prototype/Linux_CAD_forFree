#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <STEPControl_Reader.hxx>
#include <TopExp_Explorer.hxx>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>

#include "fk_blend.h"
#include "fk_body_check.h"
#include "fk_body_io.h"
#include "fk_boolean.h"
#include "fk_curve_ops.h"
#include "fk_helix.h"
#include "fk_loft.h"
#include "fk_mass.h"
#include "fk_primitives.h"
#include "fk_revolve.h"
#include "fk_step.h"
#include "fk_sweep.h"
#include "fk_tessellate.h"
#include "fk_test_profiles.h"

using namespace fktest;

namespace {

// I body di prova: primitive (facce periodiche senza cucitura, sfera intera),
// booleana, raccordi, sweep lungo un'elica, loft liscio (B-spline).
std::vector<std::pair<std::string, Body>> sampleBodies() {
    std::vector<std::pair<std::string, Body>> bodies;
    const Frame3 base(Vec3(0, 0, 0), Vec3(0, 0, 1), Vec3(1, 0, 0));
    const Body box = makeBox(base, 4.0, 3.0, 2.0);
    bodies.push_back({"box", box});
    bodies.push_back({"cilindro", makeCylinder(Frame3(Vec3(6, 0, 0), normalized(Vec3(0, 1, 1)), Vec3(1, 0, 0)), 1.0, 3.0)});
    bodies.push_back({"sfera", makeSphere(Frame3(Vec3(0, 6, 0), Vec3(0, 0, 1), Vec3(1, 0, 0)), 1.5)});
    bodies.push_back({"toro", makeTorus(Frame3(Vec3(8, 6, 0), Vec3(0, 0, 1), Vec3(1, 0, 0)), 2.0, 0.5)});
    bodies.push_back({"cono", makeCone(base, 2.0, 0.5, 3.0)});
    const Body hole = makeCylinder(Frame3(Vec3(2, 1.5, -1), Vec3(0, 0, 1), Vec3(1, 0, 0)), 0.8, 4.0);
    const Body cut = booleanOperation(box, hole, BooleanOperation::Subtract);
    bodies.push_back({"forato", cut});
    std::vector<EdgeId> top;
    for (EdgeId e : cut.edges()) {
        const Edge &edge = cut.edge(e);
        if (std::fabs(edge.curve->point(edge.range.lo).z() - 2.0) < 1e-9 && std::fabs(edge.curve->point(edge.range.hi).z() - 2.0) < 1e-9) top.push_back(e);
    }
    bodies.push_back({"raccordato", blendEdges(cut, top, 0.3, false)});
    HelixSpec spec;
    spec.frame = base;
    spec.radius = 3.0;
    spec.pitch = 2.0;
    spec.turns = 1.5;
    const auto helix = std::make_shared<HelixCurve>(spec);
    ProfileRegion circle;
    circle.outer.segments = {arcSegment(Vec2(0, 0), 0.4, 0.0, kTwoPi)};
    const Vec3 start = helix->point(0.0), tangent = normalized(helix->derivative(0.0));
    bodies.push_back({"molla", sweepRegions(Frame3(start, tangent, Vec3(0, 0, 1)), {circle}, {{helix, helix->domain()}})});
    LoftSection a, b;
    a.frame = base;
    a.loop.segments = {arcSegment(Vec2(0, 0), 2.0, 0.0, kTwoPi)};
    b.frame = Frame3(Vec3(0, 0, 3), Vec3(0, 0, 1), Vec3(1, 0, 0));
    b.loop.segments = {lineSegment(Vec2(1, 0), Vec2(1, 1)), lineSegment(Vec2(1, 1), Vec2(-1, 1)), lineSegment(Vec2(-1, 1), Vec2(-1, -1)),
                       lineSegment(Vec2(-1, -1), Vec2(1, -1)), lineSegment(Vec2(1, -1), Vec2(1, 0))};
    bodies.push_back({"loft", loftSolid({a, b}, false)});
    return bodies;
}

TopoDS_Shape readWithOcct(const std::string &content) {
    const std::string path = std::string(P_tmpdir) + "/forgekernel_step_test.step";
    {
        std::ofstream file(path, std::ios::binary);
        file << content;
    }
    STEPControl_Reader reader;
    FK_CHECK(reader.ReadFile(path.c_str()) == IFSelect_RetDone);
    reader.TransferRoots();
    std::remove(path.c_str());
    return reader.OneShape();
}

}

FK_TEST(StepWriteReadByOcct) {
    // Ogni body scritto da solo e riletto da OCCT: forma valida, volume uguale.
    for (const auto &[name, body] : sampleBodies()) {
        for (StepSchema schema : {StepSchema::AP214, StepSchema::AP242, StepSchema::AP203}) {
            ExchangeBody exchange;
            exchange.name = name;
            exchange.body = body;
            exchange.hasColor = true;
            exchange.color[0] = 0.2, exchange.color[1] = 0.5, exchange.color[2] = 0.9;
            StepWriteOptions options;
            options.schema = schema;
            const std::string content = writeStep({exchange}, options);
            const TopoDS_Shape shape = readWithOcct(content);
            FK_CHECK(!shape.IsNull());
            if (shape.IsNull()) continue;
            int solids = 0;
            for (TopExp_Explorer s(shape, TopAbs_SOLID); s.More(); s.Next()) ++solids;
            FK_CHECK(solids == 1);
            if (!BRepCheck_Analyzer(shape).IsValid()) reportFailure(__FILE__, __LINE__, name + ": forma OCCT non valida");
            GProp_GProps props;
            BRepGProp::VolumeProperties(shape, props, 1e-9);
            const double ours = massProperties(body, 1e-9).volume;
            if (std::fabs(props.Mass() - ours) > 1e-5 * ours)
                reportFailure(__FILE__, __LINE__, name + ": volume OCCT " + std::to_string(props.Mass()) + " invece di " + std::to_string(ours));
        }
    }
}

#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepOffsetAPI_ThruSections.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepPrimAPI_MakeRevol.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <BRepPrimAPI_MakeTorus.hxx>
#include <GeomAPI_Interpolate.hxx>
#include <Interface_Static.hxx>
#include <STEPCAFControl_Writer.hxx>
#include <STEPControl_Writer.hxx>
#include <TColgp_HArray1OfPnt.hxx>
#include <TDataStd_Name.hxx>
#include <TDocStd_Document.hxx>
#include <TopoDS.hxx>
#include <XCAFApp_Application.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <gp_Circ.hxx>

namespace {

std::string writeWithOcct(const TopoDS_Shape &shape, const char *unit = "MM") {
    const std::string path = std::string(P_tmpdir) + "/forgekernel_step_occt.step";
    Interface_Static::SetCVal("write.step.unit", unit);
    STEPControl_Writer writer;
    writer.Transfer(shape, STEPControl_AsIs);
    writer.Write(path.c_str());
    Interface_Static::SetCVal("write.step.unit", "MM");
    std::ifstream file(path, std::ios::binary);
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    std::remove(path.c_str());
    return content;
}

double occtVolume(const TopoDS_Shape &shape) {
    GProp_GProps props;
    BRepGProp::VolumeProperties(shape, props, 1e-10);
    return props.Mass();
}

double readVolume(const std::string &content, int expectedBodies = 1) {
    const StepReadResult read = readStep(content);
    for (const std::string &n : read.notes)
        if (n.find("non ricostruito") != std::string::npos) reportFailure(__FILE__, __LINE__, n);
    FK_CHECK(int(read.bodies.size()) == expectedBodies);
    double volume = 0.0;
    for (const ExchangeBody &b : read.bodies) {
        for (const CheckIssue &issue : checkBody(b.body)) reportFailure(__FILE__, __LINE__, describe(issue.code) + ": " + issue.message);
        volume += massProperties(b.body, 1e-9).volume;
    }
    return volume;
}

TopoDS_Face splineFace() {
    Handle(TColgp_HArray1OfPnt) points = new TColgp_HArray1OfPnt(1, 5);
    const double xy[5][2] = {{0, 0}, {1, 0.4}, {2, -0.3}, {3, 0.5}, {4, 0}};
    for (int i = 0; i < 5; ++i) points->SetValue(i + 1, gp_Pnt(xy[i][0], xy[i][1] + 1.0, 0.0));
    GeomAPI_Interpolate interpolate(points, false, 1e-9);
    interpolate.Perform();
    const TopoDS_Edge spline = BRepBuilderAPI_MakeEdge(interpolate.Curve()).Edge();
    return BRepBuilderAPI_MakeFace(BRepBuilderAPI_MakeWire(spline, BRepBuilderAPI_MakeEdge(gp_Pnt(4, 1, 0), gp_Pnt(4, 3, 0)).Edge(),
                                                           BRepBuilderAPI_MakeEdge(gp_Pnt(4, 3, 0), gp_Pnt(0, 3, 0)).Edge(),
                                                           BRepBuilderAPI_MakeEdge(gp_Pnt(0, 3, 0), gp_Pnt(0, 1, 0)).Edge())
                                       .Wire())
        .Face();
}

}

FK_TEST(StepRoundTrip) {
    // Scritti e riletti dal kernel: stessi volumi (la geometria e' la stessa, a parte le curve approssimate delle eliche).
    std::vector<ExchangeBody> bodies;
    for (const auto &[name, body] : sampleBodies()) {
        ExchangeBody e;
        e.name = name;
        e.body = body;
        bodies.push_back(e);
    }
    const StepReadResult read = readStep(writeStep(bodies));
    FK_CHECK(read.bodies.size() == bodies.size());
    for (std::size_t k = 0; k < read.bodies.size() && k < bodies.size(); ++k) {
        FK_CHECK(read.bodies[k].name == bodies[k].name);
        for (const CheckIssue &issue : checkBody(read.bodies[k].body)) reportFailure(__FILE__, __LINE__, bodies[k].name + ": " + describe(issue.code) + ": " + issue.message);
        const double v0 = massProperties(bodies[k].body, 1e-9).volume, v1 = massProperties(read.bodies[k].body, 1e-9).volume;
        if (std::fabs(v0 - v1) > 1e-8 * v0) reportFailure(__FILE__, __LINE__, bodies[k].name + ": " + std::to_string(v1) + " invece di " + std::to_string(v0));
    }
}

FK_TEST(StepReadOcctFiles) {
    // STEP scritti da OCCT (con le cuciture e i bordi degeneri che OCCT mette): i volumi.
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(gp_Pnt(-2, -2, 0), 4.0, 4.0, 2.0).Shape();
    const TopoDS_Shape cut = BRepAlgoAPI_Cut(box, BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0.5, 0, -1), gp_Dir(0, 0, 1)), 1.0, 4.0).Shape()).Shape();
    FK_CHECK_NEAR(readVolume(writeWithOcct(cut)), 32.0 - 2.0 * kPi, 1e-9 * 32.0);
    FK_CHECK_NEAR(readVolume(writeWithOcct(BRepPrimAPI_MakeSphere(gp_Pnt(1, 2, 3), 1.5).Shape())), 4.0 / 3.0 * kPi * 3.375, 1e-9);
    FK_CHECK_NEAR(readVolume(writeWithOcct(BRepPrimAPI_MakeTorus(gp_Ax2(gp_Pnt(0, 0, 0), gp_Dir(0, 1, 0)), 3.0, 1.0).Shape())), 6.0 * kPi * kPi, 1e-9);
    BRepFilletAPI_MakeFillet fillet(cut);
    for (TopExp_Explorer e(cut, TopAbs_EDGE); e.More(); e.Next()) {
        BRepAdaptor_Curve curve(TopoDS::Edge(e.Current()));
        if (curve.GetType() == GeomAbs_Line && std::fabs(curve.Line().Direction().Z()) > 0.99) fillet.Add(0.3, TopoDS::Edge(e.Current()));
    }
    fillet.Build();
    FK_CHECK_NEAR(readVolume(writeWithOcct(fillet.Shape())), occtVolume(fillet.Shape()), 1e-6 * 32.0);
    const TopoDS_Face face = splineFace();
    const TopoDS_Shape revolved = BRepPrimAPI_MakeRevol(face, gp_Ax1(gp_Pnt(0, 0, 0), gp_Dir(1, 0, 0))).Shape();
    FK_CHECK_NEAR(readVolume(writeWithOcct(revolved)), occtVolume(revolved), 1e-6 * occtVolume(revolved));
    GProp_GProps area;
    BRepGProp::SurfaceProperties(face, area, 1e-13);
    FK_CHECK_NEAR(readVolume(writeWithOcct(BRepPrimAPI_MakePrism(face, gp_Vec(0, 0, 2.5)).Shape())), area.Mass() * 2.5, 1e-9 * area.Mass());
    BRepOffsetAPI_ThruSections loft(true, false, 1e-7);
    const double radii[3] = {2.0, 1.0, 2.0};
    for (int i = 0; i < 3; ++i)
        loft.AddWire(BRepBuilderAPI_MakeWire(BRepBuilderAPI_MakeEdge(gp_Circ(gp_Ax2(gp_Pnt(0, 0, 2.0 * i), gp_Dir(0, 0, 1)), radii[i])).Edge()).Wire());
    loft.Build();
    FK_CHECK_NEAR(readVolume(writeWithOcct(loft.Shape())), occtVolume(loft.Shape()), 1e-6 * occtVolume(loft.Shape()));
    // In pollici: il box di 1" x 2" x 3" diventa di 25.4 x 50.8 x 76.2 mm.
    const TopoDS_Shape inches = BRepPrimAPI_MakeBox(25.4, 50.8, 76.2).Shape();
    FK_CHECK_NEAR(readVolume(writeWithOcct(inches, "INCH")), 25.4 * 50.8 * 76.2, 1e-9 * 25.4 * 50.8 * 76.2);
}

FK_TEST(StepReadAssembly) {
    // Assieme XCAF con due istanze posizionate dello stesso cilindro e un box.
    const Handle(XCAFApp_Application) application = XCAFApp_Application::GetApplication();
    Handle(TDocStd_Document) document;
    application->NewDocument(TCollection_ExtendedString("MDTV-XCAF"), document);
    const Handle(XCAFDoc_ShapeTool) tool = XCAFDoc_DocumentTool::ShapeTool(document->Main());
    const TDF_Label assembly = tool->NewShape();
    TDataStd_Name::Set(assembly, "gruppo");
    const TDF_Label pin = tool->AddShape(BRepPrimAPI_MakeCylinder(0.5, 2.0).Shape(), false);
    TDataStd_Name::Set(pin, "perno");
    const TDF_Label block = tool->AddShape(BRepPrimAPI_MakeBox(3.0, 2.0, 1.0).Shape(), false);
    TDataStd_Name::Set(block, "blocco");
    gp_Trsf a, b;
    a.SetTranslation(gp_Vec(10, 0, 0));
    b.SetRotation(gp_Ax1(gp_Pnt(0, 0, 0), gp_Dir(1, 0, 0)), kHalfPi);
    b.SetTranslationPart(gp_Vec(0, 7, 0));
    tool->AddComponent(assembly, pin, TopLoc_Location(a));
    tool->AddComponent(assembly, pin, TopLoc_Location(b));
    tool->AddComponent(assembly, block, TopLoc_Location());
    tool->UpdateAssemblies();
    const std::string path = std::string(P_tmpdir) + "/forgekernel_step_assembly.step";
    STEPCAFControl_Writer writer;
    writer.Transfer(document, STEPControl_AsIs);
    writer.Write(path.c_str());
    application->Close(document);
    std::ifstream file(path, std::ios::binary);
    const std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    std::remove(path.c_str());
    const StepReadResult read = readStep(content);
    FK_CHECK(read.bodies.size() == 3);
    int pins = 0;
    for (const ExchangeBody &body : read.bodies) {
        const MassProperties m = massProperties(body.body, 1e-10);
        if (body.name.find("perno") == 0) {
            ++pins;
            FK_CHECK_NEAR(m.volume, kPi * 0.25 * 2.0, 1e-10);
            // Le due istanze: (10, 0, 1) e ruotata attorno a X di 90 gradi: (0, 7 - 1, 0).
            const bool first = distance(m.centroid, Vec3(10, 0, 1)) < 1e-9, second = distance(m.centroid, Vec3(0, 6, 0)) < 1e-9;
            FK_CHECK(first || second);
        } else {
            FK_CHECK(body.name == "blocco");
            FK_CHECK_NEAR(m.volume, 6.0, 1e-10);
        }
    }
    FK_CHECK(pins == 2);
}

#include <GCPnts_AbscissaPoint.hxx>
#include <TopoDS.hxx>

#include "fk_curve_algo.h"

FK_TEST(StepCurveByOcct) {
    // Un'elica (curva senza forma STEP) e un arco di cerchio: OCCT li rilegge
    // come edge con la stessa lunghezza e gli stessi estremi.
    HelixSpec spec;
    spec.frame = Frame3(Vec3(1, 2, 3), normalized(Vec3(0, 1, 1)), Vec3(1, 0, 0));
    spec.radius = 2.0, spec.pitch = 0.7, spec.turns = 3.5, spec.taper = 0.1;
    const CurvePtr<3> helix = std::make_shared<HelixCurve>(spec);
    const CurvePtr<3> circle = std::make_shared<Circle<3>>(makeCircle(Frame3(Vec3(0, 0, 1), Vec3(0, 0, 1), Vec3(1, 0, 0)), 1.5));
    const std::vector<std::pair<CurvePtr<3>, Interval>> curves = {{helix, helix->domain()}, {circle, {0.3, 2.5}}};
    for (const auto &[curve, range] : curves) {
        ExchangeBody exchange;
        exchange.name = "curva";
        exchange.curve = curve;
        exchange.curveRange = range;
        const TopoDS_Shape shape = readWithOcct(writeStep({exchange}));
        FK_CHECK(!shape.IsNull());
        if (shape.IsNull()) continue;
        double length = 0.0;
        int edges = 0;
        for (TopExp_Explorer e(shape, TopAbs_EDGE); e.More(); e.Next(), ++edges)
            length += GCPnts_AbscissaPoint::Length(BRepAdaptor_Curve(TopoDS::Edge(e.Current())), 1e-10);
        FK_CHECK(edges == 1);
        const double expected = arcLength(*curve, range);
        FK_CHECK_NEAR(length, expected, 1e-7 * expected);
    }
}

#include <clocale>

#include "fk_exchange.h"

FK_TEST(StepNumbersIgnoreLocale) {
    // Con un locale a virgola decimale (quello di un'applicazione Qt in
    // italiano) i numeri si scrivono e si leggono ancora con il punto.
    const std::string saved = std::setlocale(LC_NUMERIC, nullptr);
    const bool comma = std::setlocale(LC_NUMERIC, "it_IT.UTF-8") || std::setlocale(LC_NUMERIC, "de_DE.UTF-8");
    for (double x : {1.5, -2.0, 1e-7, 123456789.125, 0.1, 6.02214076e23, -3.0e-300}) {
        const std::string s = detail::formatReal(x);
        FK_CHECK(s.find(',') == std::string::npos && s.find('.') != std::string::npos);
        double back = 0.0;
        FK_CHECK(detail::parseReal(s.data(), s.data() + s.size(), back) == s.size());
        FK_CHECK(back == x);
    }
    const Body box = makeBox(Frame3(Vec3(0.5, 0, 0), Vec3(0, 0, 1), Vec3(1, 0, 0)), 1.25, 2.5, 0.75);
    ExchangeBody exchange;
    exchange.name = "box";
    exchange.body = box;
    const StepReadResult read = readStep(writeStep({exchange}));
    std::setlocale(LC_NUMERIC, saved.c_str());
    (void)comma;  // senza locale a virgola il test verifica solo il formato
    FK_CHECK(read.bodies.size() == 1);
    if (!read.bodies.empty()) FK_CHECK_NEAR(massProperties(read.bodies.front().body, 1e-10).volume, 1.25 * 2.5 * 0.75, 1e-12);
}

#include <BRepGProp.hxx>

FK_TEST(StepReadSolidWorksFile) {
    // File reale di SolidWorks 2023 (AP214, 267 facce con B-spline) con edge
    // lontani dalle facce fino a 9e-4: si legge se c'e' nella radice del progetto.
    const std::string path = std::string(FORGECAD_SOURCE_DIR) + "/D260297-REV00.STEP";
    std::ifstream file(path, std::ios::binary);
    if (!file) return;
    const std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    const StepReadResult read = readStep(content);
    FK_CHECK(read.bodies.size() == 1);
    if (read.bodies.empty()) return;
    const Body &body = read.bodies.front().body;
    FK_CHECK(checkBody(body).empty());
    FK_CHECK(!body.isSheet());
    const TopoDS_Shape shape = readWithOcct(content);
    GProp_GProps props;
    BRepGProp::VolumeProperties(shape, props, 1e-9);
    FK_CHECK_NEAR(massProperties(body, 1e-9).volume, props.Mass(), 1e-6 * props.Mass());
}

FK_TEST(StepReadSignedMajorTorus) {
    const Frame3 frame(Vec3(3, -2, 7), normalized(Vec3(1, 2, 3)), Vec3(1, 0, 0));
    ExchangeBody source;
    source.body = makeTorus(frame, 10.5, 1.0);
    const std::string original = writeStep({source});
    for (const std::string type : {"TOROIDAL_SURFACE", "DEGENERATE_TOROIDAL_SURFACE"}) {
        std::string content = original;
        const auto start = content.find("TOROIDAL_SURFACE(");
        FK_CHECK(start != std::string::npos);
        if (start == std::string::npos) return;
        const auto placementEnd = content.find(',', content.find(',', start) + 1);
        const auto end = content.find(')', placementEnd);
        content.replace(placementEnd + 1, end - placementEnd - 1,
                        type == "TOROIDAL_SURFACE" ? "-10.5,13." : "-10.5,13.,.T.");
        content.replace(start, std::string("TOROIDAL_SURFACE").size(), type);
        const auto read = readStep(content);
        FK_CHECK(read.bodies.size() == 1);
        if (read.bodies.empty()) continue;
        const Body &body = read.bodies.front().body;
        FK_CHECK(checkBody(body).empty());
        FK_CHECK(body.faces().size() == 1);
        const Surface &surface = *body.face(body.faces().front()).surface;
        FK_CHECK(surface.type() == SurfaceType::Revolution);
        for (double u : {0.0, 0.7, 2.1, 5.8}) {
            for (double v : {0.0, 0.3, 1.4, 3.2, 5.7}) {
                const Vec3 radial = std::cos(u) * frame.xDir() + std::sin(u) * frame.yDir();
                const Vec3 tangent = -std::sin(u) * frame.xDir() + std::cos(u) * frame.yDir();
                const double rho = -10.5 + 13.0 * std::cos(v);
                const Vec3 expected = frame.origin() + rho * radial + 13.0 * std::sin(v) * frame.zDir();
                FK_CHECK_NEAR(distance(surface.point(u, v), expected), 0.0, 1e-10);
                const Vec3 normal = normalized(cross(rho * tangent,
                    -13.0 * std::sin(v) * radial + 13.0 * std::cos(v) * frame.zDir()));
                FK_CHECK_NEAR(distance(surface.normal(u, v), normal), 0.0, 1e-10);
            }
        }
    }
}

FK_TEST(StepReadAP0730SolidWorksFile) {
    const std::string path = std::string(FORGECAD_SOURCE_DIR) + "/File_Esempio/AP0730-REV00.STEP";
    std::ifstream file(path, std::ios::binary);
    if (!file) return;  // fixture reale facoltativa, test sintetico sempre eseguito
    const std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    const auto read = readStep(content);
    FK_CHECK(read.bodies.size() == 1);
    if (read.bodies.empty()) return;
    const Body &body = read.bodies.front().body;
    FK_CHECK(!body.isSheet());
    FK_CHECK(checkBody(body).empty());
    int revolutions = 0;
    for (FaceId id : body.faces())
        if (body.face(id).surface->type() == SurfaceType::Revolution) ++revolutions;
    FK_CHECK(revolutions == 2);
    const auto mesh = tessellate(body, {});
    // Tutte le facce si triangolano, anche quelle sottili tra un segmento e
    // un arco con freccia sotto la deflessione. L'area dei triangoli non
    // supera quella esatta: sul toro della faccia 249 un campione quasi
    // doppio (salto di 6e-12 tra le SP-curve) dava una "membrana" piatta
    // percorsa nei due versi, sei volte l'area della faccia.
    FK_CHECK(mesh.failedFaces == 0);
    FK_CHECK(mesh.faces.size() == body.faces().size());
    for (FaceId id : body.faces()) {
        const auto found = std::find_if(mesh.faces.begin(), mesh.faces.end(),
            [id](const FaceMesh &face) { return face.face == id; });
        FK_CHECK(found != mesh.faces.end());
        if (found == mesh.faces.end()) continue;
        FK_CHECK(!found->triangles.empty());
        double area = 0.0;
        for (const auto &t : found->triangles)
            area += 0.5 * norm(cross(found->points[std::size_t(t[1])] - found->points[std::size_t(t[0])],
                                     found->points[std::size_t(t[2])] - found->points[std::size_t(t[0])]));
        FK_CHECK(area <= 1.05 * faceArea(body, id, 1e-9));
    }
    const Body restored = readBodyBinary(writeBodyBinary(body));
    FK_CHECK(restored.faces().size() == body.faces().size());
    FK_CHECK(checkBody(restored).empty());
}
