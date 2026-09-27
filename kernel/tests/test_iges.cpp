#include <BRepAdaptor_Curve.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepBuilderAPI_MakeSolid.hxx>
#include <BRepBuilderAPI_Sewing.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepGProp.hxx>
#include <BRepLib.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <GProp_GProps.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <IGESControl_Controller.hxx>
#include <IGESControl_Reader.hxx>
#include <IGESControl_Writer.hxx>
#include <Interface_Static.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Shell.hxx>
#include <TopoDS_Solid.hxx>

#include <cmath>
#include <cstdio>
#include <fstream>

#include "fk_blend.h"
#include "fk_body_check.h"
#include "fk_boolean.h"
#include "fk_iges.h"
#include "fk_loft.h"
#include "fk_mass.h"
#include "fk_primitives.h"
#include "fk_revolve.h"
#include "fk_test_profiles.h"

using namespace fktest;

namespace {

std::vector<std::pair<std::string, Body>> igesBodies() {
    std::vector<std::pair<std::string, Body>> bodies;
    const Frame3 base(Vec3(0, 0, 0), Vec3(0, 0, 1), Vec3(1, 0, 0));
    const Body box = makeBox(base, 4.0, 3.0, 2.0);
    bodies.push_back({"box", box});
    bodies.push_back({"cilindro", makeCylinder(Frame3(Vec3(6, 0, 0), normalized(Vec3(0, 1, 1)), Vec3(1, 0, 0)), 1.0, 3.0)});
    bodies.push_back({"sfera", makeSphere(Frame3(Vec3(0, 6, 0), Vec3(0, 0, 1), Vec3(1, 0, 0)), 1.5)});
    bodies.push_back({"toro", makeTorus(Frame3(Vec3(8, 6, 0), Vec3(0, 0, 1), Vec3(1, 0, 0)), 2.0, 0.5)});
    bodies.push_back({"cono", makeCone(base, 2.0, 0.5, 3.0)});
    const Body cut = booleanOperation(box, makeCylinder(Frame3(Vec3(2, 1.5, -1), Vec3(0, 0, 1), Vec3(1, 0, 0)), 0.8, 4.0), BooleanOperation::Subtract);
    bodies.push_back({"forato", cut});
    std::vector<EdgeId> top;
    for (EdgeId e : cut.edges()) {
        const Edge &edge = cut.edge(e);
        if (std::fabs(edge.curve->point(edge.range.lo).z() - 2.0) < 1e-9 && std::fabs(edge.curve->point(edge.range.hi).z() - 2.0) < 1e-9) top.push_back(e);
    }
    bodies.push_back({"raccordato", blendEdges(cut, top, 0.3, false)});
    LoftSection a, b;
    a.frame = base;
    a.loop.segments = {arcSegment(Vec2(0, 0), 2.0, 0.0, kTwoPi)};
    b.frame = Frame3(Vec3(0, 0, 3), Vec3(0, 0, 1), Vec3(1, 0, 0));
    b.loop.segments = {lineSegment(Vec2(1, 0), Vec2(1, 1)), lineSegment(Vec2(1, 1), Vec2(-1, 1)), lineSegment(Vec2(-1, 1), Vec2(-1, -1)),
                       lineSegment(Vec2(-1, -1), Vec2(1, -1)), lineSegment(Vec2(1, -1), Vec2(1, 0))};
    bodies.push_back({"loft", loftSolid({a, b}, false)});
    return bodies;
}

std::string tempPath() { return std::string(P_tmpdir) + "/forgekernel_iges_test.igs"; }

TopoDS_Shape readWithOcct(const std::string &content) {
    {
        std::ofstream file(tempPath(), std::ios::binary);
        file << content;
    }
    IGESControl_Controller::Init();
    IGESControl_Reader reader;
    FK_CHECK(reader.ReadFile(tempPath().c_str()) == IFSelect_RetDone);
    reader.TransferRoots();
    std::remove(tempPath().c_str());
    return reader.OneShape();
}

// Il volume OCCT: i solidi, o le facce cucite e chiuse in un solido.
double occtVolume(const TopoDS_Shape &shape) {
    double volume = 0.0;
    int solids = 0;
    for (TopExp_Explorer s(shape, TopAbs_SOLID); s.More(); s.Next()) {
        GProp_GProps props;
        BRepGProp::VolumeProperties(s.Current(), props, 1e-9);
        volume += props.Mass();
        ++solids;
    }
    if (solids > 0) return volume;
    BRepBuilderAPI_Sewing sewing(1e-6);
    for (TopExp_Explorer f(shape, TopAbs_FACE); f.More(); f.Next()) sewing.Add(f.Current());
    sewing.Perform();
    for (TopExp_Explorer s(sewing.SewedShape(), TopAbs_SHELL); s.More(); s.Next()) {
        BRepBuilderAPI_MakeSolid solid(TopoDS::Shell(s.Current()));
        TopoDS_Solid made = solid.Solid();
        BRepLib::OrientClosedSolid(made);
        GProp_GProps props;
        BRepGProp::VolumeProperties(made, props, 1e-9);
        volume += props.Mass();
    }
    return volume;
}

std::string writeWithOcct(const TopoDS_Shape &shape, int brepMode) {
    IGESControl_Controller::Init();
    Interface_Static::SetIVal("write.iges.brep.mode", brepMode);
    IGESControl_Writer writer("MM", brepMode);
    writer.AddShape(shape);
    writer.ComputeModel();
    writer.Write(tempPath().c_str());
    std::ifstream file(tempPath(), std::ios::binary);
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    std::remove(tempPath().c_str());
    return content;
}

double ourVolume(const IgesReadResult &read) {
    for (const std::string &n : read.notes)
        if (n.find("non ricostruito") != std::string::npos || n.find("saltata") != std::string::npos) reportFailure(__FILE__, __LINE__, n);
    double volume = 0.0;
    for (const ExchangeBody &b : read.bodies) {
        for (const CheckIssue &issue : checkBody(b.body)) reportFailure(__FILE__, __LINE__, b.name + ": " + describe(issue.code) + ": " + issue.message);
        FK_CHECK(!b.body.isSheet());
        volume += massProperties(b.body, 1e-9).volume;
    }
    return volume;
}

}

FK_TEST(IgesWriteReadByOcct) {
    for (const auto &[name, body] : igesBodies()) {
        const double expected = massProperties(body, 1e-9).volume;
        for (IgesMode mode : {IgesMode::Solids, IgesMode::Surfaces}) {
            ExchangeBody exchange;
            exchange.name = name;
            exchange.body = body;
            exchange.hasColor = true;
            exchange.color[0] = 0.8, exchange.color[1] = 0.2, exchange.color[2] = 0.1;
            IgesWriteOptions options;
            options.mode = mode;
            const TopoDS_Shape shape = readWithOcct(writeIges({exchange}, options));
            FK_CHECK(!shape.IsNull());
            if (shape.IsNull()) continue;
            const double occt = occtVolume(shape);
            if (std::fabs(occt - expected) > 1e-5 * expected)
                reportFailure(__FILE__, __LINE__, name + (mode == IgesMode::Solids ? " (solidi)" : " (superfici)") + ": volume OCCT " + std::to_string(occt) +
                                                      " invece di " + std::to_string(expected));
        }
    }
}

FK_TEST(IgesRoundTrip) {
    for (IgesMode mode : {IgesMode::Solids, IgesMode::Surfaces}) {
        std::vector<ExchangeBody> bodies;
        double expected = 0.0;
        for (const auto &[name, body] : igesBodies()) {
            ExchangeBody e;
            e.name = name;
            e.body = body;
            bodies.push_back(e);
            expected += massProperties(body, 1e-9).volume;
        }
        IgesWriteOptions options;
        options.mode = mode;
        const IgesReadResult read = readIges(writeIges(bodies, options));
        FK_CHECK(read.bodies.size() == bodies.size());
        FK_CHECK_NEAR(ourVolume(read), expected, 1e-8 * expected);
        if (mode == IgesMode::Solids)
            for (std::size_t k = 0; k < read.bodies.size() && k < bodies.size(); ++k) FK_CHECK(read.bodies[k].name == bodies[k].name);
    }
}

FK_TEST(IgesReadOcctFiles) {
    // IGES di OCCT: B-rep (MSBO) e superfici limitate (144) da cucire.
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(gp_Pnt(-2, -2, 0), 4.0, 4.0, 2.0).Shape();
    const TopoDS_Shape cut = BRepAlgoAPI_Cut(box, BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0.5, 0, -1), gp_Dir(0, 0, 1)), 1.0, 4.0).Shape()).Shape();
    BRepFilletAPI_MakeFillet fillet(cut);
    for (TopExp_Explorer e(cut, TopAbs_EDGE); e.More(); e.Next()) {
        BRepAdaptor_Curve curve(TopoDS::Edge(e.Current()));
        if (curve.GetType() == GeomAbs_Line && std::fabs(curve.Line().Direction().Z()) > 0.99) fillet.Add(0.3, TopoDS::Edge(e.Current()));
    }
    fillet.Build();
    const std::vector<std::pair<std::string, TopoDS_Shape>> shapes = {
        {"forato", cut}, {"sfera", BRepPrimAPI_MakeSphere(gp_Pnt(1, 2, 3), 1.5).Shape()}, {"raccordato", fillet.Shape()}};
    for (const auto &[name, shape] : shapes) {
        GProp_GProps props;
        BRepGProp::VolumeProperties(shape, props, 1e-10);
        for (int mode : {1, 0}) {
            const std::string label = name + (mode ? " (MSBO)" : " (144)");
            double ours = 0.0;
            try {
                ours = ourVolume(readIges(writeWithOcct(shape, mode)));
            } catch (const std::exception &e) {
                reportFailure(__FILE__, __LINE__, label + ": " + e.what());
                continue;
            }
            if (std::fabs(ours - props.Mass()) > 1e-6 * props.Mass())
                reportFailure(__FILE__, __LINE__, label + ": " + std::to_string(ours) + " invece di " + std::to_string(props.Mass()));
        }
    }
}

#include <GCPnts_AbscissaPoint.hxx>

#include "fk_curve_algo.h"
#include "fk_helix.h"

FK_TEST(IgesCurveByOcct) {
    HelixSpec spec;
    spec.frame = Frame3(Vec3(1, 2, 3), normalized(Vec3(0, 1, 1)), Vec3(1, 0, 0));
    spec.radius = 2.0, spec.pitch = 0.7, spec.turns = 3.5, spec.taper = 0.1;
    const CurvePtr<3> helix = std::make_shared<HelixCurve>(spec);
    ExchangeBody exchange;
    exchange.name = "elica";
    exchange.curve = helix;
    exchange.curveRange = helix->domain();
    const TopoDS_Shape shape = readWithOcct(writeIges({exchange}));
    FK_CHECK(!shape.IsNull());
    if (shape.IsNull()) return;
    double length = 0.0;
    int edges = 0;
    for (TopExp_Explorer e(shape, TopAbs_EDGE); e.More(); e.Next(), ++edges)
        length += GCPnts_AbscissaPoint::Length(BRepAdaptor_Curve(TopoDS::Edge(e.Current())), 1e-10);
    FK_CHECK(edges == 1);
    const double expected = arcLength(*helix, helix->domain());
    FK_CHECK_NEAR(length, expected, 1e-7 * expected);
}
