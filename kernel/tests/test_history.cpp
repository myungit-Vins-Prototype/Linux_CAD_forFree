#include "fk_test.h"

#include <algorithm>
#include <cmath>

#include "fk_blend.h"
#include "fk_body_io.h"
#include "fk_bspline_surface.h"
#include "fk_sheet.h"
#include "fk_boolean.h"
#include "fk_classify.h"
#include "fk_history.h"
#include "fk_primitives.h"
#include "fk_surface_algo.h"
#include "fk_transform.h"

using namespace ForgeCad::Kernel;

namespace {

Frame3 frameAt(double x, double y, double z) { return Frame3(Vec3(x, y, z), Vec3(0, 0, 1), Vec3(1, 0, 0)); }

// Facce del body che contengono il punto (sulla superficie esatta, dentro i loop).
std::vector<int> facesThrough(const Body &body, const Vec3 &point) {
    std::vector<int> found;
    for (FaceId f : body.faces()) {
        const Surface &s = *body.face(f).surface;
        const SurfaceProjection p = projectPoint(s, point);
        if (p.distance > 1e-7) continue;
        if (classifyPointOnFace(body, f, point, 1e-7) != PointLocation::Inside) continue;
        found.push_back(f.index);
    }
    return found;
}

// Faccia con la normale uscente data che contiene il punto.
int faceAt(const Body &body, const Vec3 &point, const Vec3 &normal) {
    int best = -1;
    for (int f : facesThrough(body, point)) {
        const Face &face = body.face(FaceId(f));
        const SurfaceProjection p = projectPoint(*face.surface, point);
        Vec3 n = face.surface->normal(p.u, p.v);
        if (!face.sense) n = -n;
        if (dot(n, normal) > 0.99) best = f;
    }
    return best;
}

ElementNames baseNames(const Body &body, const std::string &tag) {
    return propagateNames(body, inferHistory({}, body), {}, tag);
}

bool contains(const std::vector<int> &values, int value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

}

FK_TEST(HistoryInteriorPointOfFaces) {
    const Body box = makeBox(frameAt(0, 0, 0), 10, 10, 10);
    const Body cylinder = makeCylinder(frameAt(5, 5, -5), 2, 20);
    const Body holed = booleanOperation(box, cylinder, BooleanOperation::Subtract);
    for (const Body *body : {&box, &cylinder, &holed})
        for (FaceId f : body->faces()) {
            Vec3 p;
            Vec2 uv;
            FK_CHECK(faceInteriorPoint(*body, f, p, &uv));
            const Surface &s = *body->face(f).surface;
            Vec3 at[1];
            s.evaluate(uv.x(), uv.y(), 0, at);
            FK_CHECK(distance(at[0], p) <= 1e-9);
        }
}

FK_TEST(HistorySubtractCylinderThroughBox) {
    const Body box = makeBox(frameAt(0, 0, 0), 10, 10, 10);
    const Body cylinder = makeCylinder(frameAt(5, 5, -5), 2, 20);
    const Body result = booleanOperation(box, cylinder, BooleanOperation::Subtract);
    const BodyHistory h = inferHistory({&box, &cylinder}, result);
    FK_CHECK(h.operandCount() == 2);

    const int boxTop = faceAt(box, Vec3(1, 1, 10), Vec3(0, 0, 1));
    const int boxSide = faceAt(box, Vec3(0, 5, 5), Vec3(-1, 0, 0));
    const int resTop = faceAt(result, Vec3(1, 1, 10), Vec3(0, 0, 1));
    const int resSide = faceAt(result, Vec3(0, 5, 5), Vec3(-1, 0, 0));
    FK_CHECK(boxTop >= 0 && boxSide >= 0 && resTop >= 0 && resSide >= 0);
    // Faccia superiore forata: Modified; fianco intatto: Same.
    FK_CHECK(h.evolution(EntityKind::Face, 0, boxTop) == Evolution::Modified);
    FK_CHECK(h.descendants(EntityKind::Face, 0, boxTop) == std::vector<int>{resTop});
    FK_CHECK(h.evolution(EntityKind::Face, 0, boxSide) == Evolution::Unchanged);
    FK_CHECK(h.descendants(EntityKind::Face, 0, boxSide) == std::vector<int>{resSide});

    // Fianco del cilindro: diventa la parete del foro (rifilata); coperchi cancellati.
    const int cylSide = faceAt(cylinder, Vec3(7, 5, 5), Vec3(1, 0, 0));
    const int cylTop = faceAt(cylinder, Vec3(5, 5, 15), Vec3(0, 0, 1));
    const int hole = faceAt(result, Vec3(7, 5, 5), Vec3(-1, 0, 0));
    FK_CHECK(cylSide >= 0 && cylTop >= 0 && hole >= 0);
    FK_CHECK(h.descendants(EntityKind::Face, 1, cylSide) == std::vector<int>{hole});
    FK_CHECK(h.deleted(EntityKind::Face, 1, cylTop));

    // Il cerchio del foro sulla faccia superiore e' un edge nuovo, generato dalle facce su cui giace.
    int circle = -1;
    for (EdgeId e : result.edges()) {
        const Edge &edge = result.edge(e);
        const Vec3 a = edge.curve->point(edge.range.lo);
        if (std::fabs(a.z() - 10) < 1e-9 && std::fabs(distance(Vec3(a.x(), a.y(), 0), Vec3(5, 5, 0)) - 2) < 1e-9)
            circle = e.index;
    }
    FK_CHECK(circle >= 0);
    if (circle >= 0) {
        bool fromBoxTop = false, fromCylinder = false;
        for (const HistoryLink &l : h.origins(EntityKind::Edge, circle)) {
            FK_CHECK(l.relation == HistoryRelation::Generated);
            if (l.operand == 0 && l.kind == EntityKind::Face && l.index == boxTop) fromBoxTop = true;
            if (l.operand == 1 && l.kind == EntityKind::Face && l.index == cylSide) fromCylinder = true;
        }
        FK_CHECK(fromBoxTop && fromCylinder);
    }

    // Nomi: la faccia superiore conserva il nome del box, il foro quello del cilindro.
    const ElementNames boxNames = baseNames(box, "box");
    const ElementNames cylNames = baseNames(cylinder, "cyl");
    const ElementNames names = propagateNames(result, h, {&boxNames, &cylNames}, "cut");
    FK_CHECK(names.primary(EntityKind::Face, resTop) == boxNames.primary(EntityKind::Face, boxTop));
    FK_CHECK(names.primary(EntityKind::Face, hole) == cylNames.primary(EntityKind::Face, cylSide));
    FK_CHECK(names.find(EntityKind::Face, boxNames.primary(EntityKind::Face, boxTop)) == std::vector<int>{resTop});
}

FK_TEST(HistorySplitFaceKeepsAmbiguousName) {
    // Un taglio passante divide la faccia superiore in due: stesso nome per i due pezzi.
    const Body box = makeBox(frameAt(0, 0, 0), 10, 10, 10);
    const Body slot = makeBox(frameAt(4, -1, 5), 2, 12, 10);
    const Body result = booleanOperation(box, slot, BooleanOperation::Subtract);
    const BodyHistory h = inferHistory({&box, &slot}, result);
    const int boxTop = faceAt(box, Vec3(1, 1, 10), Vec3(0, 0, 1));
    const std::vector<int> pieces = h.descendants(EntityKind::Face, 0, boxTop);
    FK_CHECK(pieces.size() == 2);
    FK_CHECK(h.evolution(EntityKind::Face, 0, boxTop) == Evolution::Split);
    const ElementNames boxNames = baseNames(box, "box");
    const ElementNames slotNames = baseNames(slot, "slot");
    const ElementNames names = propagateNames(result, h, {&boxNames, &slotNames}, "cut");
    FK_CHECK(names.find(EntityKind::Face, boxNames.primary(EntityKind::Face, boxTop)).size() == 2);
    // La faccia di fondo della scanalatura viene dal coperchio inferiore dello strumento.
    const int slotBottom = faceAt(slot, Vec3(5, 5, 5), Vec3(0, 0, -1));
    const int floor = faceAt(result, Vec3(5, 5, 5), Vec3(0, 0, 1));
    FK_CHECK(slotBottom >= 0 && floor >= 0);
    FK_CHECK(contains(h.descendants(EntityKind::Face, 1, slotBottom), floor));
}

FK_TEST(HistoryUniteMergesCoplanarFaces) {
    const Body a = makeBox(frameAt(0, 0, 0), 10, 10, 10);
    const Body b = makeBox(frameAt(10, 0, 0), 10, 10, 10);
    const Body result = booleanOperation(a, b, BooleanOperation::Unite);
    const BodyHistory h = inferHistory({&a, &b}, result);
    const int aTop = faceAt(a, Vec3(5, 5, 10), Vec3(0, 0, 1));
    const int bTop = faceAt(b, Vec3(15, 5, 10), Vec3(0, 0, 1));
    const int top = faceAt(result, Vec3(5, 5, 10), Vec3(0, 0, 1));
    FK_CHECK(top >= 0 && top == faceAt(result, Vec3(15, 5, 10), Vec3(0, 0, 1)));
    FK_CHECK(h.evolution(EntityKind::Face, 0, aTop) == Evolution::Merged);
    FK_CHECK(h.evolution(EntityKind::Face, 1, bTop) == Evolution::Merged);
    // Le facce di contatto spariscono.
    const int aRight = faceAt(a, Vec3(10, 5, 5), Vec3(1, 0, 0));
    FK_CHECK(h.deleted(EntityKind::Face, 0, aRight));
    const ElementNames an = baseNames(a, "a"), bn = baseNames(b, "b");
    const ElementNames names = propagateNames(result, h, {&an, &bn}, "u");
    FK_CHECK(names.faces[std::size_t(top)].size() == 2);
}

FK_TEST(HistoryBlendFaceGeneratedFromEdge) {
    const Body box = makeBox(frameAt(0, 0, 0), 10, 10, 10);
    const EdgeId e = nearestEdge(box, Vec3(10, 5, 10), 1e-6);
    FK_CHECK(e.valid());
    const Body result = blendEdges(box, {e}, 1.0, false);
    const BodyHistory h = inferHistory({&box}, result);
    FK_CHECK(h.deleted(EntityKind::Edge, 0, e.index));
    const std::vector<int> fillet = h.generated(EntityKind::Face, EntityKind::Edge, 0, e.index);
    FK_CHECK(fillet.size() == 1);
    // Le due facce dello spigolo sono rifilate.
    const int top = faceAt(box, Vec3(5, 5, 10), Vec3(0, 0, 1));
    const int right = faceAt(box, Vec3(10, 5, 5), Vec3(1, 0, 0));
    FK_CHECK(h.evolution(EntityKind::Face, 0, top) == Evolution::Modified);
    FK_CHECK(h.evolution(EntityKind::Face, 0, right) == Evolution::Modified);

    // Il nome della faccia del raccordo dipende dal nome dello spigolo, non dagli
    // indici o dal raggio: uguale con raggio diverso.
    const ElementNames bn = baseNames(box, "box");
    const ElementNames n1 = propagateNames(result, h, {&bn}, "fillet");
    const Body result2 = blendEdges(box, {e}, 2.0, false);
    const BodyHistory h2 = inferHistory({&box}, result2);
    const ElementNames n2 = propagateNames(result2, h2, {&bn}, "fillet");
    const std::vector<int> fillet2 = h2.generated(EntityKind::Face, EntityKind::Edge, 0, e.index);
    FK_CHECK(fillet2.size() == 1);
    if (fillet.size() == 1 && fillet2.size() == 1) {
        const std::string name = n1.primary(EntityKind::Face, fillet[0]);
        FK_CHECK(!name.empty());
        FK_CHECK(name == n2.primary(EntityKind::Face, fillet2[0]));
        FK_CHECK(name.rfind("fillet:", 0) == 0);
    }
}

FK_TEST(HistoryComposeAcrossOperations) {
    const Body box = makeBox(frameAt(0, 0, 0), 10, 10, 10);
    const EdgeId e = nearestEdge(box, Vec3(10, 5, 10), 1e-6);
    const Body blended = blendEdges(box, {e}, 1.0, false);
    const Body cylinder = makeCylinder(frameAt(3, 3, -5), 1, 20);
    const Body result = booleanOperation(blended, cylinder, BooleanOperation::Subtract);
    const BodyHistory first = inferHistory({&box}, blended);
    const BodyHistory second = inferHistory({&blended, &cylinder}, result);
    const BodyHistory both = BodyHistory::compose(first, second, 0);
    FK_CHECK(both.operandCount() == 2);  // box, cilindro
    const int boxTop = faceAt(box, Vec3(1, 1, 10), Vec3(0, 0, 1));
    const int top = faceAt(result, Vec3(1, 1, 10), Vec3(0, 0, 1));
    FK_CHECK(both.descendants(EntityKind::Face, 0, boxTop) == std::vector<int>{top});
    const std::vector<int> fillet = both.generated(EntityKind::Face, EntityKind::Edge, 0, e.index);
    FK_CHECK(fillet.size() == 1);
    const int cylSide = faceAt(cylinder, Vec3(4, 3, 5), Vec3(1, 0, 0));
    FK_CHECK(both.descendants(EntityKind::Face, 1, cylSide).size() == 1);
}

FK_TEST(HistorySameIndexTransform) {
    const Body box = makeBox(frameAt(0, 0, 0), 10, 10, 10);
    const Body moved = transformBody(box, Transform3::translation(Vec3(3, 4, 5)));
    const BodyHistory h = sameIndexHistory(box, moved);
    for (FaceId f : box.faces()) FK_CHECK(h.descendants(EntityKind::Face, 0, f.index) == std::vector<int>{f.index});
    const ElementNames bn = baseNames(box, "box");
    const ElementNames names = propagateNames(moved, h, {&bn}, "move");
    for (FaceId f : box.faces()) FK_CHECK(names.faces[std::size_t(f.index)] == bn.faces[std::size_t(f.index)]);
    // Anche la genealogia geometrica riconosce la stessa topologia su una copia.
    const BodyHistory same = inferHistory({&box}, box);
    for (FaceId f : box.faces()) FK_CHECK(same.evolution(EntityKind::Face, 0, f.index) == Evolution::Unchanged);
    FK_CHECK_THROWS(sameIndexHistory(box, makeCylinder(frameAt(0, 0, 0), 1, 1)));
}

FK_TEST(HistoryLabelledFacesAndLongNames) {
    const Body box = makeBox(frameAt(0, 0, 0), 10, 10, 10);
    FaceLabels labels(std::size_t(box.counts().faces + 8));
    for (FaceId f : box.faces()) labels[std::size_t(f.index)] = "side.s" + std::to_string(f.index);
    const ElementNames names = propagateNames(box, inferHistory({}, box), {}, "extrude7", &labels);
    for (FaceId f : box.faces()) FK_CHECK(names.primary(EntityKind::Face, f.index) == "extrude7:side.s" + std::to_string(f.index));
    // Nomi di edge e vertici composti e sempre non vuoti, al massimo 160 caratteri.
    for (EdgeId e : box.edges()) {
        const std::string n = names.primary(EntityKind::Edge, e.index);
        FK_CHECK(!n.empty() && n.size() <= 160);
    }
    for (VertexId v : box.vertices()) FK_CHECK(!names.primary(EntityKind::Vertex, v.index).empty());
    const std::string longTag(200, 'x');
    const ElementNames hashed = baseNames(box, longTag);
    for (FaceId f : box.faces()) FK_CHECK(hashed.primary(EntityKind::Face, f.index).size() <= 220);
}

// Dopo la rilettura gli appoggi non condividono piu' i puntatori. La
// genealogia e i nomi devono essere gli stessi anche per le facce NURBS.
FK_TEST(HistoryPreservesNamesAfterReload) {
    const Body box = makeBox(Frame3(), 10, 10, 10);
    const Body first = blendEdges(box, {nearestEdge(box,Vec3(5,0,10),1e-8)},0.5,false);
    const Body next = blendEdges(first,{nearestEdge(first,Vec3(5,10,0),1e-8)},0.75,false);
    const Body savedFirst=readBodyBinary(writeBodyBinary(first)), savedNext=readBodyBinary(writeBodyBinary(next));
    const auto rootHistory=inferHistory({},first);
    const auto names=propagateNames(first,rootHistory,{},"f1");
    const auto history=inferHistory({&first},next);
    const auto expected=propagateNames(next,history,{&names},"f2");
    const auto savedNames=propagateNames(savedFirst,inferHistory({},savedFirst),{},"f1");
    const auto actual=propagateNames(savedNext,inferHistory({&savedFirst},savedNext),{&savedNames},"f2");
    FK_CHECK(actual.faces==expected.faces);
    FK_CHECK(actual.edges==expected.edges);
    FK_CHECK(actual.vertices==expected.vertices);
    // Modifiche reali non devono ereditare l'identita' dell'appoggio.
    Body sheet=makePlaneSheet(Frame3(),1.0);
    std::vector<Vec3> poles;
    for(int i=0;i<4;++i)for(int j=0;j<4;++j)poles.emplace_back(-1+2.0*i/3,-1+2.0*j/3,0);
    const std::vector<double> knots{-1,-1,-1,-1,1,1,1,1};
    const FaceId face=sheet.faces().front();
    sheet.face(face).surface=std::make_shared<BSplineSurface>(3,3,knots,knots,4,4,poles);
    Body extended=sheet;
    extended.face(face).surface=std::make_shared<BSplineSurface>(extendBSplineSurface(
        static_cast<const BSplineSurface&>(*sheet.face(face).surface),{-1.25,1.5},{-1.5,1.25}));
    const auto preserved=inferHistory({&sheet},extended);
    FK_CHECK(preserved.evolution(EntityKind::Face,0,face.index)==Evolution::Unchanged);
    poles[5][2]=0.01;
    extended.face(face).surface=std::make_shared<BSplineSurface>(3,3,knots,knots,4,4,poles);
    FK_CHECK(inferHistory({&sheet},extended).deleted(EntityKind::Face,0,face.index));
}

FK_TEST(HistoryRootWithNullOperands) {
    const Body body=makeBox(Frame3(),2,3,4);
    const auto root=inferHistory({},body), nullRoot=inferHistory({nullptr,nullptr},body);
    FK_CHECK(nullRoot.operandCount()==2);
    const auto names=propagateNames(body,root,{},"root");
    const auto nullNames=propagateNames(body,nullRoot,{nullptr,nullptr},"root");
    FK_CHECK(names.faces==nullNames.faces&&names.edges==nullNames.edges&&names.vertices==nullNames.vertices);
    for(auto id:body.faces())FK_CHECK(nullRoot.origins(EntityKind::Face,id.index).empty());
    CancellationScope cancelled(std::make_shared<std::atomic<bool>>(true));
    bool stopped=false;try{inferHistory({},body);}catch(const OperationCancelled &){stopped=true;}
    FK_CHECK(stopped);
}
