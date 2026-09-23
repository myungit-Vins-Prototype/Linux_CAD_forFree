#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <GProp_GProps.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <gp_Ax2.hxx>

#include "fk_body_check.h"
#include "fk_curve_algo.h"
#include "fk_primitives.h"
#include "fk_test_util.h"

using namespace fktest;

namespace {

bool valid(const Body &body) {
    const std::vector<CheckIssue> issues = checkBody(body);
    for (const CheckIssue &issue : issues) reportFailure(__FILE__, __LINE__, describe(issue.code) + ": " + issue.message);
    return issues.empty();
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

double occtEdgeLength(const TopoDS_Shape &shape) {
    // SkipShared: ogni edge una volta sola (di default conta ogni uso da parte di una faccia).
    GProp_GProps properties;
    BRepGProp::LinearProperties(shape, properties, true);
    return properties.Mass();
}

// Poligono stellato semplice: angoli crescenti, raggi tra 5 e 10.
std::vector<Vec2> starPolygon(std::mt19937 &rng, int count) {
    std::vector<Vec2> polygon;
    for (int i = 0; i < count; ++i) {
        const double angle = kTwoPi * (i + uniform(rng, 0.1, 0.9)) / count, radius = uniform(rng, 5.0, 10.0);
        polygon.push_back(Vec2(radius * std::cos(angle), radius * std::sin(angle)));
    }
    return polygon;
}

double signedArea(const std::vector<Vec2> &polygon) {
    double area = 0.0;
    for (std::size_t i = 0; i < polygon.size(); ++i) area += cross(polygon[i], polygon[(i + 1) % polygon.size()]);
    return 0.5 * area;
}

TopoDS_Wire occtWire(const Frame3 &frame, const std::vector<Vec2> &polygon) {
    BRepBuilderAPI_MakePolygon maker;
    for (const Vec2 &p : polygon) maker.Add(toPnt(frame.toGlobal(Vec3(p.x(), p.y(), 0.0))));
    maker.Close();
    return maker.Wire();
}

}

FK_TEST(BoxMatchesOcct) {
    std::mt19937 rng(90);
    for (int trial = 0; trial < 20; ++trial) {
        const Frame3 frame = randomFrame(rng);
        const double dx = uniform(rng, 0.1, 50), dy = uniform(rng, 0.1, 50), dz = uniform(rng, 0.1, 50);
        const Body box = makeBox(frame, dx, dy, dz);
        FK_CHECK(valid(box));
        const TopologyCounts c = box.counts();
        FK_CHECK(c.vertices == 8 && c.edges == 12 && c.faces == 6 && c.loops == 6 && c.shells == 1);
        FK_CHECK(shellGenus(box, box.shells().front()) == 0);

        const TopoDS_Shape reference = BRepPrimAPI_MakeBox(gp_Ax2(toPnt(frame.origin()), toDir(frame.zDir()), toDir(frame.xDir())), dx, dy, dz).Shape();
        FK_CHECK(countShapes(reference, TopAbs_VERTEX) == c.vertices);
        FK_CHECK(countShapes(reference, TopAbs_EDGE) == c.edges);
        FK_CHECK(countShapes(reference, TopAbs_FACE) == c.faces);
        FK_CHECK_NEAR(totalEdgeLength(box), occtEdgeLength(reference), 1e-9 * (dx + dy + dz));
        FK_CHECK_NEAR(totalEdgeLength(box), 4.0 * (dx + dy + dz), 1e-9 * (dx + dy + dz));
    }
}

FK_TEST(PrismWithHolesMatchesOcct) {
    std::mt19937 rng(91);
    for (int trial = 0; trial < 30; ++trial) {
        const Frame3 frame = randomFrame(rng);
        std::vector<Vec2> outer = starPolygon(rng, uniformInt(rng, 3, 12));
        if (trial % 2) std::reverse(outer.begin(), outer.end());  // qualsiasi verso in ingresso
        std::vector<std::vector<Vec2>> holes;
        const int holeCount = trial % 4;
        for (int k = 0; k < holeCount; ++k) {
            const Vec2 c(2.2 * std::cos(kTwoPi * k / 3), 2.2 * std::sin(kTwoPi * k / 3));
            const double s = 0.4;
            holes.push_back({c + Vec2(-s, -s), c + Vec2(s, -s), c + Vec2(s, s), c + Vec2(-s, s)});
        }
        const double height = uniform(rng, 0.5, 20.0);
        const Body prism = makePrism(frame, outer, holes, height);
        FK_CHECK(valid(prism));
        const int n = int(outer.size()) + 4 * holeCount;
        const TopologyCounts c = prism.counts();
        FK_CHECK(c.vertices == 2 * n && c.edges == 3 * n && c.faces == n + 2);
        FK_CHECK(c.rings() == 2 * holeCount);
        FK_CHECK(shellGenus(prism, prism.shells().front()) == holeCount);

        // Stesso solido con OCCT: faccia (filo esterno antiorario, fori orari) estrusa.
        std::vector<Vec2> occtOuter = outer;
        if (signedArea(occtOuter) < 0.0) std::reverse(occtOuter.begin(), occtOuter.end());
        BRepBuilderAPI_MakeFace faceMaker(occtWire(frame, occtOuter), true);
        for (std::vector<Vec2> hole : holes) {
            std::reverse(hole.begin(), hole.end());  // i quadrati sono antiorari
            faceMaker.Add(occtWire(frame, hole));
        }
        const TopoDS_Shape reference = BRepPrimAPI_MakePrism(faceMaker.Face(), gp_Vec(toPnt(Vec3()), toPnt(height * frame.zDir()))).Shape();
        FK_CHECK(countShapes(reference, TopAbs_VERTEX) == c.vertices);
        FK_CHECK(countShapes(reference, TopAbs_EDGE) == c.edges);
        FK_CHECK(countShapes(reference, TopAbs_FACE) == c.faces);
        FK_CHECK_NEAR(totalEdgeLength(prism), occtEdgeLength(reference), 1e-9 * occtEdgeLength(reference));
    }
    FK_CHECK_THROWS(makePrism(Frame3(), {Vec2(0, 0), Vec2(1, 0)}, {}, 1.0));
    FK_CHECK_THROWS(makePrism(Frame3(), {Vec2(0, 0), Vec2(1, 0), Vec2(2, 0)}, {}, 1.0));
}

// Differenza voluta con OCCT: niente cucitura. OCCT ha 3 edge (due cerchi e
// il seam, percorso due volte dal fianco), noi 2 e il fianco ha due loop.
FK_TEST(CylinderWithoutSeam) {
    std::mt19937 rng(92);
    for (int trial = 0; trial < 20; ++trial) {
        const Frame3 frame = randomFrame(rng);
        const double radius = uniform(rng, 0.1, 30.0), height = uniform(rng, 0.1, 30.0);
        const Body cylinder = makeCylinder(frame, radius, height);
        FK_CHECK(valid(cylinder));
        const TopologyCounts c = cylinder.counts();
        FK_CHECK(c.vertices == 2 && c.edges == 2 && c.faces == 3 && c.loops == 4);
        FK_CHECK(shellGenus(cylinder, cylinder.shells().front()) == 0);

        const TopoDS_Shape reference =
            BRepPrimAPI_MakeCylinder(gp_Ax2(toPnt(frame.origin()), toDir(frame.zDir()), toDir(frame.xDir())), radius, height).Shape();
        FK_CHECK(countShapes(reference, TopAbs_VERTEX) == 2);
        FK_CHECK(countShapes(reference, TopAbs_EDGE) == 3);
        FK_CHECK(countShapes(reference, TopAbs_FACE) == 3);
        FK_CHECK_NEAR(totalEdgeLength(cylinder), 2.0 * kTwoPi * radius, 1e-9 * radius);
        FK_CHECK_NEAR(totalEdgeLength(cylinder) + height, occtEdgeLength(reference), 1e-9 * (radius + height));
    }
}
