#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRep_Tool.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>

#include "fk_body_check.h"
#include "fk_boolean.h"
#include "fk_extrude.h"
#include "fk_mass.h"
#include "fk_primitives.h"
#include "fk_test_profiles.h"
#include "fk_unify.h"

using namespace fktest;

namespace {

gp_Ax2 ax2(const Frame3 &frame) { return gp_Ax2(toPnt(frame.origin()), toDir(frame.zDir()), toDir(frame.xDir())); }

struct Solid {
    Body body;
    TopoDS_Shape shape;
};

Solid box(const Frame3 &frame, double dx, double dy, double dz) {
    return {makeBox(frame, dx, dy, dz), BRepPrimAPI_MakeBox(ax2(frame), dx, dy, dz).Shape()};
}

Solid cylinder(const Frame3 &frame, double r, double h) {
    return {makeCylinder(frame, r, h), BRepPrimAPI_MakeCylinder(ax2(frame), r, h).Shape()};
}

Frame3 at(double x, double y, double z) { return Frame3(Vec3(x, y, z), Vec3(0, 0, 1), Vec3(1, 0, 0)); }

// Facce ed edge (senza le cuciture delle superfici periodiche, che il nuovo
// kernel non ha) dopo ShapeUpgrade_UnifySameDomain.
void occtCounts(const TopoDS_Shape &shape, int &faces, int &edges) {
    ShapeUpgrade_UnifySameDomain unify(shape, true, true, false);
    unify.Build();
    const TopoDS_Shape result = unify.Shape();
    TopTools_IndexedMapOfShape faceMap, edgeMap;
    TopExp::MapShapes(result, TopAbs_FACE, faceMap);
    TopExp::MapShapes(result, TopAbs_EDGE, edgeMap);
    faces = faceMap.Extent();
    edges = 0;
    for (int i = 1; i <= edgeMap.Extent(); ++i) {
        const TopoDS_Edge &edge = TopoDS::Edge(edgeMap(i));
        bool seam = false;
        for (TopExp_Explorer explorer(result, TopAbs_FACE); explorer.More() && !seam; explorer.Next())
            seam = BRep_Tool::IsClosed(edge, TopoDS::Face(explorer.Current()));
        if (!seam) ++edges;
    }
}

TopoDS_Shape occtBoolean(const TopoDS_Shape &a, const TopoDS_Shape &b, BooleanOperation operation) {
    switch (operation) {
    case BooleanOperation::Unite: return BRepAlgoAPI_Fuse(a, b).Shape();
    case BooleanOperation::Intersect: return BRepAlgoAPI_Common(a, b).Shape();
    default: return BRepAlgoAPI_Cut(a, b).Shape();
    }
}

// Booleana con e senza fusione: stessi volume e area, risultato valido, e
// dopo la fusione lo stesso numero di facce ed edge di OCCT.
void compareUnified(const Solid &a, const Solid &b, BooleanOperation operation) {
    BooleanOptions raw;
    raw.unifySameDomain = false;
    Body unified, split;
    try {
        unified = booleanOperation(a.body, b.body, operation);
        split = booleanOperation(a.body, b.body, operation, raw);
    } catch (const std::exception &error) {
        reportFailure(__FILE__, __LINE__, error.what());
        return;
    }
    FK_CHECK(checkBody(unified).empty());
    const MassProperties mu = massProperties(unified), ms = massProperties(split);
    FK_CHECK_NEAR(mu.volume, ms.volume, 1e-10 * ms.volume);
    FK_CHECK_NEAR(mu.area, ms.area, 1e-10 * ms.area);
    int faces, edges;
    occtCounts(occtBoolean(a.shape, b.shape, operation), faces, edges);
    FK_CHECK(unified.counts().faces == faces);
    FK_CHECK(unified.counts().edges == edges);
}

void compareAllUnified(const Solid &a, const Solid &b) {
    for (BooleanOperation operation : {BooleanOperation::Unite, BooleanOperation::Intersect, BooleanOperation::Subtract})
        compareUnified(a, b, operation);
}

}

// Blocchi sullo stesso piano: affiancati (un blocco solo), a L, con una tasca,
// uno sopra l'altro.
FK_TEST(UnifyBoxes) {
    const Solid a = box(at(0, 0, 0), 10, 6, 4);
    compareUnified(a, box(at(10, 0, 0), 5, 6, 4), BooleanOperation::Unite);  // stesso blocco allungato: 6 facce
    compareAllUnified(a, box(at(6, 3, 0), 8, 6, 4));                         // a L e il suo complemento
    compareAllUnified(a, box(at(2, 2, 2), 3, 2, 5));                         // tasca dall'alto
    compareUnified(a, box(at(0, 0, 4), 10, 6, 3), BooleanOperation::Unite);   // uno sopra l'altro, stessa impronta
    compareUnified(a, box(at(3, -1, 0), 2, 8, 4), BooleanOperation::Subtract);  // scanalatura passante
}

// Cilindri: dischi complanari (due cerchi estrusi dallo stesso piano), fori,
// cilindro sopra un blocco, fori incrociati.
FK_TEST(UnifyCylinders) {
    const Solid block = box(at(0, 0, 0), 20, 12, 6);
    compareAllUnified(cylinder(at(0, 0, 0), 5, 8), cylinder(at(6, 0, 0), 4, 8));
    compareAllUnified(block, cylinder(at(6, 6, -2), 3, 10));
    compareUnified(block, cylinder(at(14, 6, 6), 3, 5), BooleanOperation::Unite);
    compareUnified(block, cylinder(at(14, 6, 0), 3, 6), BooleanOperation::Unite);  // stessa altezza del blocco
    const Solid crossed = cylinder(Frame3(Vec3(-2, 6, 3), Vec3(1, 0, 0), Vec3(0, 1, 0)), 2, 24);
    compareUnified(block, crossed, BooleanOperation::Subtract);
}

// La fusione da sola su un corpo gia' minimo non cambia nulla.
FK_TEST(UnifyIdempotent) {
    const ProfileRegion region = buildProfile(roundedRectangle(Vec2(0, 0), 20.0, 12.0, 3.0), 1e-6).regions.front();
    const Body body = makeExtrusion(Frame3(), region, 5.0);
    const Body unified = unifySameDomain(body);
    FK_CHECK(unified.counts().faces == body.counts().faces);
    FK_CHECK(unified.counts().edges == body.counts().edges);
    FK_CHECK(unified.counts().vertices == body.counts().vertices);
    FK_CHECK(checkBody(unified).empty());
}
