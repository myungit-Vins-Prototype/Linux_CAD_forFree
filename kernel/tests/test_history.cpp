#include "fk_test.h"

#include <algorithm>
#include <cmath>
#include <tuple>
#include <set>

#include "fk_blend.h"
#include "fk_blend_loop.h"
#include "fk_blend_surface.h"
#include "fk_extrude.h"
#include "fk_history_tools.h"
#include "fk_exchange.h"
#include "fk_project.h"
#include "fk_draft.h"
#include "fk_shell.h"
#include "fk_thicken.h"
#include "fk_fill.h"
#include "fk_boundary.h"
#include "fk_planar.h"
#include "fk_loft.h"
#include "fk_sweep.h"
#include "fk_curve_ops.h"
#include "fk_body_io.h"
#include "fk_bspline_surface.h"
#include "fk_sheet.h"
#include "fk_sew.h"
#include "fk_boolean.h"
#include "fk_classify.h"
#include "fk_history.h"
#include "fk_offset.h"
#include "fk_pcurve.h"
#include "fk_primitives.h"
#include "fk_revolve.h"
#include "fk_surface_algo.h"
#include "fk_transform.h"
#include "fk_unify.h"

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

FK_TEST(HistoryDeclaredOriginsOverrideGeometry) {
    const Body box = makeBox(Frame3(), 2, 3, 4);
    const auto faces = box.faces();
    const auto edge = box.edges().front();
    const auto vertex = box.vertices().front();
    BodyHistory declared = makeHistory({&box}, box);
    declared.declareOrigins(EntityKind::Face, faces[0].index,
        {{0, EntityKind::Face, faces[1].index, HistoryRelation::Modified}});
    declared.declareOrigins(EntityKind::Edge, edge.index, {});
    declared.declareOrigins(EntityKind::Vertex, vertex.index,
        {{0, EntityKind::Face, faces[1].index, HistoryRelation::Generated}});
    const auto history = resolveHistory({&box}, box, &declared);
    const auto &origin = history.origins(EntityKind::Face, faces[0].index);
    FK_CHECK(origin.size() == 1 && origin.front().index == faces[1].index
        && origin.front().relation == HistoryRelation::Modified);
    FK_CHECK(history.isDeclared(EntityKind::Face, faces[0].index));
    FK_CHECK(history.isDeclared(EntityKind::Edge, edge.index));
    FK_CHECK(history.origins(EntityKind::Edge, edge.index).empty());
    FK_CHECK(history.isDeclared(EntityKind::Vertex, vertex.index));
    FK_CHECK(history.origins(EntityKind::Vertex, vertex.index).size() == 1
        && history.origins(EntityKind::Vertex, vertex.index).front().relation == HistoryRelation::Generated);
    FK_CHECK(!history.isDeclared(EntityKind::Face, faces[2].index));
    FK_CHECK(history.evolution(EntityKind::Face, 0, faces[2].index) == Evolution::Unchanged);
    const auto inferred = inferHistory({&box}, box);
    FK_CHECK(!inferred.isDeclared(EntityKind::Face, faces[0].index));
    FK_CHECK(inferred.evolution(EntityKind::Face, 0, faces[0].index) == Evolution::Unchanged);
}

FK_TEST(HistoryRejectsInvalidDeclarations) {
    const Body box = makeBox(Frame3(), 2, 3, 4);
    const auto faces = box.faces();
    BodyHistory declared = makeHistory({&box}, box);
    FK_CHECK_THROWS(declared.declareOrigins(EntityKind::Face, faces[0].index,
        {{1, EntityKind::Face, faces[0].index, HistoryRelation::Same}}));
    FK_CHECK_THROWS(declared.declareOrigins(EntityKind::Face, faces[0].index,
        {{0, EntityKind::Face, 10000, HistoryRelation::Same}}));
    FK_CHECK_THROWS(declared.declareOrigins(EntityKind::Face, 10000, {}));
    declared.declareOrigins(EntityKind::Face, faces[1].index,
        {{0, EntityKind::Face, faces[0].index, HistoryRelation::Same}});
    Body missing = box;
    missing.face(faces[0]).alive = false;
    FK_CHECK_THROWS(resolveHistory({&missing}, box, &declared));
    Body deadResult = box;
    deadResult.face(faces[1]).alive = false;
    FK_CHECK_THROWS(resolveHistory({&box}, deadResult, &declared));
    const auto wrong = makeHistory({}, box);
    FK_CHECK_THROWS(resolveHistory({&box}, box, &wrong));
    const Body sphere = makeSphere(Frame3(), 1);
    FK_CHECK_THROWS(resolveHistory({&box}, sphere, &declared));
}

FK_TEST(HistoryDeclaredCompositionCoverage) {
    const Body box = makeBox(Frame3(), 2, 3, 4);
    const auto faces = box.faces();
    BodyHistory first = makeHistory({&box}, box), second = makeHistory({&box}, box);
    first.declareOrigins(EntityKind::Face, faces[0].index,
        {{0, EntityKind::Face, faces[1].index, HistoryRelation::Modified}});
    second.declareOrigins(EntityKind::Face, faces[2].index,
        {{0, EntityKind::Face, faces[0].index, HistoryRelation::Modified}});
    second.declareOrigins(EntityKind::Face, faces[3].index,
        {{0, EntityKind::Face, faces[1].index, HistoryRelation::Modified}});
    second.declareOrigins(EntityKind::Face, faces[4].index, {});
    auto composed = BodyHistory::compose(first, second, 0);
    FK_CHECK(composed.isDeclared(EntityKind::Face, faces[2].index));
    FK_CHECK(composed.origins(EntityKind::Face, faces[2].index).size() == 1
        && composed.origins(EntityKind::Face, faces[2].index).front().index == faces[1].index);
    FK_CHECK(!composed.isDeclared(EntityKind::Face, faces[3].index));
    FK_CHECK(composed.isDeclared(EntityKind::Face, faces[4].index));
    FK_CHECK(composed.origins(EntityKind::Face, faces[4].index).empty());
    first.addOrigin(EntityKind::Face, faces[1].index,
        {0, EntityKind::Face, faces[5].index, HistoryRelation::Same});
    composed = BodyHistory::compose(first, second, 0);
    FK_CHECK(!composed.isDeclared(EntityKind::Face, faces[3].index));
    FK_CHECK(composed.origins(EntityKind::Face, faces[3].index).size() == 1);
}

FK_TEST(HistoryOffsetKeepsFaceNamesAcrossSurfaceChanges) {
    const Body box = makeBox(Frame3(), 2, 3, 4);
    const auto names = baseNames(box, "box");
    const auto faces = box.faces();
    for (bool sew : {false, true})
        for (double d : {-0.2, 0.3}) {
            // Selezione non ordinata e con duplicati: gli indici di uscita
            // sono densi e diversi da quelli delle facce originali scelte.
            const auto offset = offsetFaces(box, {faces[4], faces[2], faces[4]}, d, 1e-7, sew);
            const auto history = resolveHistory({&box}, offset.body, &offset.history);
            const auto next = propagateNames(offset.body, history, {&names}, "offset");
            FK_CHECK(offset.body.faces().size() == 2);
            for (FaceId face : offset.body.faces()) {
                FK_CHECK(history.isDeclared(EntityKind::Face, face.index));
                const auto &links = history.origins(EntityKind::Face, face.index);
                FK_CHECK(links.size() == 1);
                if (links.size() != 1) continue;
                FK_CHECK(links.front().index == faces[2].index || links.front().index == faces[4].index);
                FK_CHECK(links.front().relation == HistoryRelation::Modified);
                FK_CHECK(next.primary(EntityKind::Face, face.index) == names.primary(EntityKind::Face, links.front().index));
                FK_CHECK(!history.isDeclared(EntityKind::Edge, offset.body.edges().front().index));
            }
            FK_CHECK(history.deleted(EntityKind::Face, 0, faces[0].index));
        }
    const Body sphere = makeSphere(Frame3(), 2);
    const auto offset = offsetFaces(sphere, sphere.faces(), 0.25);
    FK_CHECK(offset.history.isDeclared(EntityKind::Face, offset.body.faces().front().index));
    FK_CHECK(offset.history.evolution(EntityKind::Face, 0, sphere.faces().front().index) == Evolution::Modified);
}

namespace {
Body creasedHistorySheet(bool trimmed) {
    const auto surface = std::make_shared<BSplineSurface>(1, 1,
        std::vector<double>{0,0,0.5,1,1}, std::vector<double>{0,0,1,1}, 3, 2,
        std::vector<Vec3>{Vec3(0,0,0),Vec3(0,1,0),Vec3(1,0,0),Vec3(1,1,0),Vec3(2,0,0.2),Vec3(2,1,0.2)});
    const double lo = trimmed ? 0.1 : 0, hi = trimmed ? 0.9 : 1;
    const Interval range{lo, hi};
    const std::vector<Vec3> points{surface->point(lo,lo), surface->point(hi,lo), surface->point(hi,hi), surface->point(lo,hi)};
    const std::vector<Body::BuildEdge> edges{{0,1,surface->vIso(lo),range,0}, {1,2,surface->uIso(hi),range,0},
        {3,2,surface->vIso(hi),range,0}, {0,3,surface->uIso(lo),range,0}};
    Body::BuildFace face;
    face.surface = surface;
    face.loops = {{{0,true,std::make_shared<Line<2>>(Vec2(0,lo),Vec2(1,0)),0},
        {1,true,std::make_shared<Line<2>>(Vec2(hi,0),Vec2(0,1)),0},
        {2,false,std::make_shared<Line<2>>(Vec2(0,hi),Vec2(1,0)),0},
        {3,false,std::make_shared<Line<2>>(Vec2(lo,0),Vec2(0,1)),0}}};
    return Body::buildSheet(points, edges, {face});
}
}

FK_TEST(HistoryOffsetComposesRectangularAndTrimmedCreases) {
    for (bool trimmed : {false, true})
        for (bool sew : {false, true}) {
            const Body source = creasedHistorySheet(trimmed);
            const auto names = baseNames(source, "source");
            const auto offset = offsetFaces(source, source.faces(), 0.1, 1e-7, sew);
            FK_CHECK(offset.body.faces().size() == 2);
            const auto history = resolveHistory({&source}, offset.body, &offset.history);
            const auto next = propagateNames(offset.body, history, {&names}, "offset");
            const auto face = source.faces().front();
            FK_CHECK(history.operandCount() == 1);
            FK_CHECK(history.evolution(EntityKind::Face, 0, face.index) == Evolution::Split);
            for (FaceId f : offset.body.faces()) {
                FK_CHECK(history.isDeclared(EntityKind::Face, f.index));
                const auto &links = history.origins(EntityKind::Face, f.index);
                FK_CHECK(links.size() == 1 && links.front().index == face.index);
                FK_CHECK(next.primary(EntityKind::Face, f.index) == names.primary(EntityKind::Face, face.index));
            }
        }
}

FK_TEST(HistoryBooleanDeclaresCutSplitAndUnchangedFaces) {
    const Body box = makeBox(Frame3(), 10, 10, 10);
    const Body cylinder = makeCylinder(frameAt(5, 5, -5), 2, 20);
    const Body slot = makeBox(frameAt(4, -1, 5), 2, 12, 10);
    const int top = faceAt(box, Vec3(1, 1, 10), Vec3(0, 0, 1));
    const int side = faceAt(box, Vec3(0, 5, 5), Vec3(-1, 0, 0));
    const auto boxNames = baseNames(box, "box");
    for (bool unify : {false, true}) {
        BooleanOptions options; options.unifySameDomain = unify;
        BodyHistory declared;
        const Body hole = booleanOperation(box, cylinder, BooleanOperation::Subtract, options, &declared);
        FK_CHECK(writeBodyBinary(hole) == writeBodyBinary(booleanOperation(box, cylinder, BooleanOperation::Subtract, options)));
        const auto history = resolveHistory({&box, &cylinder}, hole, &declared);
        for (FaceId face : hole.faces()) FK_CHECK(history.isDeclared(EntityKind::Face, face.index));
        FK_CHECK(history.evolution(EntityKind::Face, 0, top) == Evolution::Modified);
        FK_CHECK(history.evolution(EntityKind::Face, 0, side) == Evolution::Unchanged);
        const int cylinderSide = faceAt(cylinder, Vec3(7, 5, 5), Vec3(1, 0, 0));
        const int wall = faceAt(hole, Vec3(7, 5, 5), Vec3(-1, 0, 0));
        FK_CHECK(wall >= 0 && contains(history.descendants(EntityKind::Face, 1, cylinderSide), wall));
        const Body split = booleanOperation(box, slot, BooleanOperation::Subtract, options, &declared);
        FK_CHECK(declared.evolution(EntityKind::Face, 0, top) == Evolution::Split);
        const auto slotNames = baseNames(slot, "slot");
        const auto names = propagateNames(split, declared, {&boxNames, &slotNames}, "cut");
        FK_CHECK(names.find(EntityKind::Face, boxNames.primary(EntityKind::Face, top)).size() == 2);
        for (FaceId face : split.faces()) FK_CHECK(declared.isDeclared(EntityKind::Face, face.index));
    }
}

FK_TEST(HistoryBooleanAndUnifyComposeEveryMergedFace) {
    const Body a = makeBox(Frame3(), 10, 10, 10);
    const Body b = makeBox(frameAt(10, 0, 0), 10, 10, 10);
    BooleanOptions options; options.unifySameDomain = false;
    BodyHistory first, unified;
    const Body pieces = booleanOperation(a, b, BooleanOperation::Unite, options, &first);
    const Body result = unifySameDomain(pieces, 1e-6, &unified);
    FK_CHECK(writeBodyBinary(result) == writeBodyBinary(unifySameDomain(pieces)));
    const auto history = BodyHistory::compose(first, unified, 0);
    const int aTop = faceAt(a, Vec3(5, 5, 10), Vec3(0, 0, 1));
    const int bTop = faceAt(b, Vec3(15, 5, 10), Vec3(0, 0, 1));
    const int top = faceAt(result, Vec3(5, 5, 10), Vec3(0, 0, 1));
    FK_CHECK(history.isDeclared(EntityKind::Face, top));
    FK_CHECK(history.origins(EntityKind::Face, top).size() == 2);
    FK_CHECK(history.evolution(EntityKind::Face, 0, aTop) == Evolution::Merged);
    FK_CHECK(history.evolution(EntityKind::Face, 1, bTop) == Evolution::Merged);
    for (FaceId face : result.faces()) FK_CHECK(history.isDeclared(EntityKind::Face, face.index));
    BodyHistory direct;
    const Body combined = booleanOperation(a, b, BooleanOperation::Unite, {}, &direct);
    FK_CHECK(writeBodyBinary(combined) == writeBodyBinary(result));
    FK_CHECK(direct.origins(EntityKind::Face, top).size() == 2);
    const auto an = baseNames(a, "a"), bn = baseNames(b, "b");
    const auto names = propagateNames(result, history, {&an, &bn}, "union");
    FK_CHECK(names.faces[std::size_t(top)].size() == 2);
}

FK_TEST(HistoryBooleanRetainsBothCoincidentOperands) {
    const Body a = makeBox(Frame3(), 3, 4, 5);
    const Body b = makeBox(Frame3(), 3, 4, 5);
    for (auto operation : {BooleanOperation::Unite, BooleanOperation::Intersect})
        for (bool unify : {false, true}) {
            BooleanOptions options; options.unifySameDomain = unify;
            BodyHistory history;
            const Body result = booleanOperation(a, b, operation, options, &history);
            FK_CHECK(result.faces().size() == 6);
            for (FaceId face : result.faces()) {
                const auto &links = history.origins(EntityKind::Face, face.index);
                FK_CHECK(history.isDeclared(EntityKind::Face, face.index));
                FK_CHECK(links.size() == 2);
                if (links.size() == 2) FK_CHECK(links[0].operand != links[1].operand);
            }
        }
}

FK_TEST(HistoryBooleanEmptyDisjointAndFailureOutputs) {
    const Body a = makeBox(Frame3(), 2, 3, 4);
    const Body far = makeBox(frameAt(20, 0, 0), 2, 3, 4);
    BodyHistory history;
    const Body empty = booleanOperation(a, far, BooleanOperation::Intersect, {}, &history);
    FK_CHECK(empty.faces().empty() && history.operandCount() == 2);
    for (FaceId face : a.faces()) FK_CHECK(history.deleted(EntityKind::Face, 0, face.index));
    for (bool unify : {false, true}) {
        BooleanOptions options; options.unifySameDomain = unify;
        const Body unchanged = booleanOperation(a, far, BooleanOperation::Subtract, options, &history);
        FK_CHECK(unchanged.faces().size() == a.faces().size());
        for (FaceId face : a.faces()) FK_CHECK(history.evolution(EntityKind::Face, 0, face.index) == Evolution::Unchanged);
        for (FaceId face : unchanged.faces()) FK_CHECK(history.isDeclared(EntityKind::Face, face.index));
    }
    const Body vanished = booleanOperation(a, a, BooleanOperation::Subtract, {}, &history);
    FK_CHECK(vanished.faces().empty() && history.operandCount() == 2);
    const Body sheet = makePlaneSheet(Frame3(), 2);
    FK_CHECK_THROWS(booleanOperation(sheet, sheet, BooleanOperation::Unite, {}, &history));
    FK_CHECK(history.operandCount() == 0);
}

FK_TEST(HistoryImprintDeclaresBeforeAndAfterUnify) {
    const Body sheet = makePlaneSheet(Frame3(), 2);
    ImprintCurve cut;
    cut.face = sheet.faces().front();
    cut.curve = std::make_shared<Line<3>>(Vec3(0, -2, 0), Vec3(0, 1, 0));
    cut.range = {0, 4}; cut.loOnEdge = cut.hiOnEdge = true;
    cut.loPoint = Vec3(0, -2, 0); cut.hiPoint = Vec3(0, 2, 0);
    for (bool unify : {false, true}) {
        BooleanOptions options; options.unifySameDomain = unify;
        BodyHistory history;
        const Body result = imprintCurves(sheet, {cut}, {}, options, nullptr, &history);
        FK_CHECK(result.faces().size() == (unify ? 1u : 2u));
        FK_CHECK(history.operandCount() == 1);
        for (FaceId face : result.faces()) {
            FK_CHECK(history.isDeclared(EntityKind::Face, face.index));
            const auto &origins = history.origins(EntityKind::Face, face.index);
            FK_CHECK(origins.size() == 1 && origins.front().index == sheet.faces().front().index);
        }
    }
}

FK_TEST(HistoryBooleanDeclarationsAreThreadDeterministic) {
    const Body a = makeBox(Frame3(), 10, 10, 10);
    const Body b = makeBox(frameAt(4, -1, 5), 2, 12, 10);
    std::string geometry;
    std::vector<std::tuple<int, int, int, int, int>> origins;
    for (int threads : {1, 4}) {
        BooleanOptions options; options.threads = threads;
        BodyHistory history;
        const Body result = booleanOperation(a, b, BooleanOperation::Subtract, options, &history);
        std::vector<std::tuple<int, int, int, int, int>> current;
        for (FaceId face : result.faces()) {
            FK_CHECK(history.isDeclared(EntityKind::Face, face.index));
            for (const auto &link : history.origins(EntityKind::Face, face.index))
                current.emplace_back(face.index, link.operand, int(link.kind), link.index, int(link.relation));
        }
        if (threads == 1) { geometry = writeBodyBinary(result); origins = current; }
        else { FK_CHECK(writeBodyBinary(result) == geometry); FK_CHECK(current == origins); }
    }
}

namespace {
void checkBlendDeclarations(const Body &input, const Body &result, const BodyHistory &history) {
    FK_CHECK(history.operandCount() == 1);
    for (FaceId f : result.faces()) {
        FK_CHECK(history.isDeclared(EntityKind::Face, f.index));
        FK_CHECK(!history.origins(EntityKind::Face, f.index).empty());
        for (const auto &link : history.origins(EntityKind::Face, f.index)) {
            FK_CHECK(link.operand == 0);
            if (link.kind == EntityKind::Face) FK_CHECK(input.contains(FaceId(link.index)));
            else if (link.kind == EntityKind::Edge) FK_CHECK(input.contains(EdgeId(link.index)) && link.relation == HistoryRelation::Generated);
            else FK_CHECK(input.contains(VertexId(link.index)) && link.relation == HistoryRelation::Generated);
        }
    }
    // Verifica anche tutte le dimensioni e gli indici delle dichiarazioni
    // ausiliarie di edge/vertici conservati dal montaggio senza booleane.
    const auto completed = resolveHistory({&input}, result, &history);
    for (FaceId f : result.faces()) FK_CHECK(completed.origins(EntityKind::Face, f.index).size() == history.origins(EntityKind::Face, f.index).size());
}
}

FK_TEST(HistoryBlendDeclaresTrimmedFacesAndGeneratedEdge) {
    const Body box = makeBox(Frame3(), 10, 10, 10);
    const EdgeId edge = nearestEdge(box, Vec3(10, 5, 10), 1e-8);
    const int top = faceAt(box, Vec3(5, 5, 10), Vec3(0, 0, 1));
    const int left = faceAt(box, Vec3(0, 5, 5), Vec3(-1, 0, 0));
    const auto names = baseNames(box, "box");
    for (bool chamfer : {false, true}) {
        std::string generatedName;
        for (double radius : {1.0, 2.0}) {
            BodyHistory history;
            const Body result = blendEdges(box, {edge}, radius, chamfer, &history);
            checkBlendDeclarations(box, result, history);
            FK_CHECK(writeBodyBinary(result) == writeBodyBinary(blendEdges(box, {edge}, radius, chamfer)));
            FK_CHECK(history.evolution(EntityKind::Face, 0, top) == Evolution::Modified);
            FK_CHECK(history.evolution(EntityKind::Face, 0, left) == Evolution::Unchanged);
            const auto generated = history.generated(EntityKind::Face, EntityKind::Edge, 0, edge.index);
            FK_CHECK(generated.size() == 1);
            const auto next = propagateNames(result, history, {&names}, "blend");
            if (generated.size() == 1) {
                const auto name = next.primary(EntityKind::Face, generated.front());
                if (radius == 1.0) generatedName = name;
                else FK_CHECK(name == generatedName);
            }
        }
    }
}

FK_TEST(HistoryBlendCornerComesFromOriginalVertex) {
    const Body box = makeBox(Frame3(), 10, 6, 4);
    std::vector<EdgeId> edges;
    for (const Vec3 &p : {Vec3(5, 0, 4), Vec3(10, 3, 4), Vec3(10, 0, 2)}) edges.push_back(nearestEdge(box, p, 1e-8));
    VertexId corner;
    for (VertexId v : box.vertices()) if (distance(box.vertex(v).point, Vec3(10, 0, 4)) < 1e-8) corner = v;
    FK_CHECK(corner.valid());
    for (bool chamfer : {false, true}) {
        BodyHistory history;
        const Body result = blendEdges(box, edges, 1, chamfer, &history);
        checkBlendDeclarations(box, result, history);
        FK_CHECK(history.generated(EntityKind::Face, EntityKind::Vertex, 0, corner.index).size() == 1);
        for (EdgeId edge : edges) FK_CHECK(!history.generated(EntityKind::Face, EntityKind::Edge, 0, edge.index).empty());
    }
}

FK_TEST(HistoryBlendClosedPlanarEdgeKeepsOriginalSource) {
    ProfileRegion region;
    region.outer.segments.push_back({std::make_shared<Ellipse<2>>(Vec2(0, 0), Vec2(1, 0), Vec2(0, 1), 3, 2), {0, kTwoPi}});
    const Body body = makeExtrusion(Frame3(), region, 4);
    const EdgeId edge = nearestEdge(body, Vec3(3, 0, 4), 1e-8);
    FK_CHECK(edge.valid() && body.edgeStart(edge) == body.edgeEnd(edge));
    for (bool chamfer : {false, true}) {
        BodyHistory direct, dispatched;
        const Body result = blendPlanarChains(body, {edge}, 0.3, chamfer, nullptr, FaceId(), &direct);
        const Body other = blendEdges(body, {edge}, 0.3, chamfer, &dispatched);
        checkBlendDeclarations(body, result, direct);
        checkBlendDeclarations(body, other, dispatched);
        FK_CHECK(direct.generated(EntityKind::Face, EntityKind::Edge, 0, edge.index).size() == 2);
        FK_CHECK(dispatched.generated(EntityKind::Face, EntityKind::Edge, 0, edge.index).size() == 2);
        FK_CHECK(writeBodyBinary(result) == writeBodyBinary(other));
    }
}

FK_TEST(HistoryBlendGeneralChainsAndOpenSheet) {
    const Body box = makeBox(Frame3(), 4, 3, 2);
    const int top = faceAt(box, Vec3(2, 1.5, 2), Vec3(0, 0, 1));
    std::vector<EdgeId> contour;
    for (LoopId loop : box.face(FaceId(top)).loops)
        for (FinId fin : box.loopFins(loop)) contour.push_back(box.fin(fin).edge);
    for (bool chamfer : {false, true}) {
        BodyHistory history;
        const Body result = blendSurfaceChains(box, contour, 0.3, chamfer, nullptr, &history);
        checkBlendDeclarations(box, result, history);
        for (EdgeId e : contour) FK_CHECK(history.generated(EntityKind::Face, EntityKind::Edge, 0, e.index).size() == 1);
        FK_CHECK(writeBodyBinary(result) == writeBodyBinary(blendSurfaceChains(box, contour, 0.3, chamfer)));
    }
    const EdgeId shared = contour.front();
    const Body sheet = facesAsSheet(box, {box.finFace(box.edge(shared).forward), box.finFace(box.edge(shared).backward)});
    EdgeId joint;
    for (EdgeId e : sheet.edges()) if (!sheet.isLaminar(e)) joint = e;
    FK_CHECK(joint.valid());
    BodyHistory history;
    const Body result = blendEdges(sheet, {joint}, 0.2, false, &history);
    FK_CHECK(result.isSheet());
    checkBlendDeclarations(sheet, result, history);
    FK_CHECK(history.generated(EntityKind::Face, EntityKind::Edge, 0, joint.index).size() == 1);
}

FK_TEST(HistoryAsymmetricChamferAndRejectedOutputs) {
    const Body box = makeBox(Frame3(), 10, 10, 10);
    const EdgeId edge = nearestEdge(box, Vec3(10, 5, 10), 1e-8);
    const std::vector<ChamferSides> sides{{Vec3(0, 0, 1), 1, 2}};
    BodyHistory history;
    const Body result = chamferEdges(box, {edge}, sides, &history);
    checkBlendDeclarations(box, result, history);
    FK_CHECK(writeBodyBinary(result) == writeBodyBinary(chamferEdges(box, {edge}, sides)));
    FK_CHECK(history.generated(EntityKind::Face, EntityKind::Edge, 0, edge.index).size() == 1);
    FK_CHECK_THROWS(blendEdges(box, {EdgeId(-1)}, 1, false, &history));
    FK_CHECK(history.operandCount() == 0);
    FK_CHECK_THROWS(chamferEdges(box, {edge}, {}, &history));
    FK_CHECK(history.operandCount() == 0);
    history = resolveHistory({&box}, result, nullptr);
    FK_CHECK_THROWS(blendPlanarChains(box, {edge}, 0, false, nullptr, FaceId(), &history));
    FK_CHECK(history.operandCount() == 0);
    history = resolveHistory({&box}, result, nullptr);
    FK_CHECK_THROWS(blendSurfaceChains(box, {edge}, 0, false, nullptr, &history));
    FK_CHECK(history.operandCount() == 0);
}

namespace {
ProfileSegment historyProfileLine(const Vec2 &a, const Vec2 &b, int source) {
    const double length = distance(a, b);
    return {std::make_shared<Line<2>>(a, (b-a)/length), {0, length}, source};
}
void checkExtrusionSources(const Body &body, const ExtrusionProvenance &provenance, const Frame3 &frame,
                           double height, const std::vector<ProfileSegment> &input) {
    FK_CHECK(provenance.faces.size() == body.faces().size());
    std::set<int> faces;
    for (const auto &origin : provenance.faces) {
        FK_CHECK(body.contains(origin.face) && faces.insert(origin.face.index).second);
        Vec3 point;
        if (origin.role == ExtrusionFaceRole::Side) {
            const auto found = std::find_if(input.begin(), input.end(), [&](const auto &segment) { return segment.source == origin.source; });
            FK_CHECK(found != input.end());
            if (found == input.end()) continue;
            const Vec2 p = found->curve->point(0.5*(found->range.lo+found->range.hi));
            point = frame.toGlobal(Vec3(p.x(), p.y(), 0.5*height));
        } else {
            point = frame.origin() + (origin.role == ExtrusionFaceRole::Start ? 0 : height)*frame.zDir();
        }
        FK_CHECK(projectPoint(*body.face(origin.face).surface, point).distance < 1e-8);
    }
}
}

FK_TEST(HistoryExtrusionSourcesSurviveLoopOrderingAndReversal) {
    std::vector<ProfileSegment> segments{
        historyProfileLine(Vec2(0,0),Vec2(0,6),10), historyProfileLine(Vec2(0,6),Vec2(8,6),11),
        historyProfileLine(Vec2(8,6),Vec2(8,0),12), historyProfileLine(Vec2(8,0),Vec2(0,0),13),
        historyProfileLine(Vec2(2,2),Vec2(4,2),20), historyProfileLine(Vec2(4,2),Vec2(4,4),21),
        historyProfileLine(Vec2(4,4),Vec2(2,4),22), historyProfileLine(Vec2(2,4),Vec2(2,2),23)};
    std::swap(segments[1], segments[6]); segments[3] = reversed(segments[3]);
    const auto profile = buildProfile(segments, 1e-7);
    FK_CHECK(profile.regions.size() == 1 && profile.regions.front().holes.size() == 1);
    const Frame3 frame(Vec3(1,2,3), normalized(Vec3(1,2,3)), Vec3(1,0,0));
    for (double height : {2.5,-2.5}) {
        ExtrusionProvenance history;
        const Body result = makeExtrusion(frame, profile.regions.front(), height, &history);
        checkExtrusionSources(result, history, frame, height, segments);
        FK_CHECK(writeBodyBinary(result) == writeBodyBinary(makeExtrusion(frame, profile.regions.front(), height)));
        int starts=0, ends=0, sides=0;
        for (const auto &origin : history.faces) {
            starts += origin.role == ExtrusionFaceRole::Start;
            ends += origin.role == ExtrusionFaceRole::End;
            sides += origin.role == ExtrusionFaceRole::Side;
        }
        FK_CHECK(starts == 1 && ends == 1 && sides == 8);
    }
    ProfileRegion wrongWinding = profile.regions.front();
    wrongWinding.outer = reversed(wrongWinding.outer);
    for (auto &hole : wrongWinding.holes) hole = reversed(hole);
    ExtrusionProvenance indexed;
    makeExtrusion(frame, wrongWinding, 2.5, &indexed);
    for (const auto &origin : indexed.faces) {
        if (origin.role != ExtrusionFaceRole::Side) continue;
        const auto &loop = origin.loop == 0 ? wrongWinding.outer : wrongWinding.holes.at(std::size_t(origin.loop - 1));
        FK_CHECK(origin.segment >= 0 && origin.segment < int(loop.segments.size()));
        if (origin.segment >= 0 && origin.segment < int(loop.segments.size()))
            FK_CHECK(loop.segments[std::size_t(origin.segment)].source == origin.source);
    }
}

FK_TEST(HistoryExtrusionClosedCurvesAndOpenChains) {
    for (bool circle : {true,false}) {
        ProfileSegment segment;
        if (circle) segment.curve=std::make_shared<Circle<2>>(Vec2(),Vec2(1,0),Vec2(0,1),2);
        else segment.curve=std::make_shared<Ellipse<2>>(Vec2(),Vec2(1,0),Vec2(0,1),3,2);
        segment.range={0,kTwoPi};segment.source=91;
        ProfileRegion region;region.outer.segments={segment};
        ExtrusionProvenance origins;
        const Body result=makeExtrusion(Frame3(),region,-3,&origins);
        checkExtrusionSources(result,origins,Frame3(),-3,{segment});
        FK_CHECK(origins.faces.size()==3);
    }
    const std::vector<ProfileSegment> segments{historyProfileLine(Vec2(0,0),Vec2(2,0),101),historyProfileLine(Vec2(2,0),Vec2(2,3),102)};
    const std::vector<ProfileLoop> chains{{}, {segments}};
    for(double height:{3.0,-3.0}) {
        ExtrusionProvenance origins;
        const Body result=makeSheetExtrusion(Frame3(),chains,height,&origins);
        checkExtrusionSources(result,origins,Frame3(),height,segments);
        FK_CHECK(result.isSheet() && origins.faces.size()==2);
        for(const auto &origin:origins.faces) FK_CHECK(origin.role==ExtrusionFaceRole::Side && origin.loop==1);
    }
    ExtrusionProvenance failed;failed.faces.push_back({});
    FK_CHECK_THROWS(makeExtrusion(Frame3(),{},1,&failed));FK_CHECK(failed.faces.empty());
    failed.faces.push_back({});
    FK_CHECK_THROWS(makeSheetExtrusion(Frame3(),chains,0,&failed));FK_CHECK(failed.faces.empty());
}

FK_TEST(HistoryDeclaredFaceAliasesOverrideGeometricLabels) {
    const Body box=makeBox(Frame3(),2,3,4);
    BodyHistory history=makeHistory({},box);
    FaceAliases aliases(std::size_t(history.resultSize(EntityKind::Face)));
    FaceLabels fallback(aliases.size(),"wrong");
    for(FaceId f:box.faces()) {history.declareOrigins(EntityKind::Face,f.index,{});aliases[std::size_t(f.index)]={"side.L7","side.L2","side.L7"};}
    const auto names=propagateNames(box,history,{},"extrude",&fallback,&aliases);
    for(FaceId f:box.faces()) FK_CHECK(names.faces[std::size_t(f.index)]==std::vector<std::string>({"extrude:side.L2","extrude:side.L7"}));
}

FK_TEST(HistoryExtractAndSewFacesPreserveDeclaredSources) {
    const Body box=makeBox(Frame3(),2,3,4);
    const auto source=box.faces();
    BodyHistory extract;
    const Body sheet=facesAsSheet(box,{source[2],source[0]},&extract);
    for(FaceId f:sheet.faces()) {
        FK_CHECK(extract.isDeclared(EntityKind::Face,f.index));
        const auto &links=extract.origins(EntityKind::Face,f.index);
        FK_CHECK(links.size()==1 && sheet.face(f).surface==box.face(FaceId(links.front().index)).surface);
    }
    const Body other=facesAsSheet(box,{source[1]});
    BodyHistory sewn;
    const auto result=sewSheets({&sheet,&other},1e-7,false,&sewn);
    for(FaceId f:result.body.faces()) {
        const auto &links=sewn.origins(EntityKind::Face,f.index);
        FK_CHECK(sewn.isDeclared(EntityKind::Face,f.index) && links.size()==1);
        if(links.size()==1) {
            const Body &input=links.front().operand==0?sheet:other;
            FK_CHECK(result.body.face(f).surface==input.face(FaceId(links.front().index)).surface);
        }
    }
    FK_CHECK_THROWS(facesAsSheet(box,{},&extract));FK_CHECK(extract.operandCount()==0);
    FK_CHECK_THROWS(sewSheets({},1e-7,false,&sewn));FK_CHECK(sewn.operandCount()==0);
}

namespace {
ProfileRegion historyRevolutionRegion(bool hole = false, bool onAxis = false) {
    ProfileRegion region;
    const double x = onAxis ? 0 : 1;
    const Vec2 p[] = {Vec2(x,0),Vec2(5,0),Vec2(5,4),Vec2(x,4)};
    for (int i=0;i<4;++i) region.outer.segments.push_back(historyProfileLine(p[i],p[(i+1)%4],10+i));
    if (hole) {
        ProfileLoop loop;
        const Vec2 q[] = {Vec2(2,1),Vec2(3,1),Vec2(3,2),Vec2(2,2)};
        for (int i=0;i<4;++i) loop.segments.push_back(historyProfileLine(q[i],q[(i+1)%4],20+i));
        region.holes.push_back(loop);
    }
    return region;
}
void checkRevolutionSources(const Body &body, const RevolutionProvenance &provenance, const Frame3 &frame,
                            const std::vector<ProfileLoop> &loops, double angle, bool sheet = false) {
    std::set<int> covered;
    int starts=0, ends=0;
    for (const auto &origin : provenance.faces) {
        FK_CHECK(body.contains(origin.face));
        covered.insert(origin.face.index);
        Vec3 point;
        if (origin.role == RevolutionFaceRole::Side) {
            FK_CHECK(origin.loop>=0 && origin.loop<int(loops.size()));
            if(origin.loop<0 || origin.loop>=int(loops.size()))continue;
            const auto &loop=loops[std::size_t(origin.loop)];
            FK_CHECK(origin.segment>=0 && origin.segment<int(loop.segments.size()));
            if(origin.segment<0 || origin.segment>=int(loop.segments.size()))continue;
            const auto &segment=loop.segments[std::size_t(origin.segment)];
            FK_CHECK(origin.source==segment.source);
            const Vec2 p=segment.curve->point(0.5*(segment.range.lo+segment.range.hi));
            const double a=std::fabs(angle)<kTwoPi ? 0.5*angle : 0.7;
            point=frame.toGlobal(Vec3(p.x()*std::cos(a),p.x()*std::sin(a),p.y()));
        } else {
            FK_CHECK(!sheet && std::fabs(angle)<kTwoPi);
            const double a=origin.role==RevolutionFaceRole::Start?0:angle;
            point=frame.toGlobal(Vec3(4*std::cos(a),4*std::sin(a),3));
            starts+=origin.role==RevolutionFaceRole::Start;
            ends+=origin.role==RevolutionFaceRole::End;
            FK_CHECK(origin.loop==-1 && origin.segment==-1 && origin.source==-1);
        }
        FK_CHECK(projectPoint(*body.face(origin.face).surface,point).distance<1e-7);
    }
    FK_CHECK(covered.size()==body.faces().size());
    if(!sheet && std::fabs(angle)<kTwoPi)FK_CHECK(starts==1 && ends==1);
    else FK_CHECK(starts==0 && ends==0);
}
}

FK_TEST(HistoryRevolutionLoopSourcesAndAxis) {
    const Frame3 frame(Vec3(1,2,3),normalized(Vec3(1,2,3)),Vec3(1,0,0));
    for(bool onAxis:{false,true})for(bool reverse:{false,true}) {
        auto region=historyRevolutionRegion(true,onAxis);
        if(reverse)region.outer=reversed(region.outer);
        RevolutionProvenance origins;
        const Body body=makeRevolution(frame,region,kTwoPi,&origins);
        checkRevolutionSources(body,origins,frame,{region.outer,region.holes.front()},kTwoPi);
        FK_CHECK(origins.faces.size()==std::size_t(onAxis?7:8));
        FK_CHECK(writeBodyBinary(body)==writeBodyBinary(makeRevolution(frame,region)));
    }
}

FK_TEST(HistoryRevolutionSignedAngularCaps) {
    const Frame3 frame(Vec3(1,2,3),normalized(Vec3(1,2,3)),Vec3(1,0,0));
    for(bool onAxis:{false,true})for(double angle:{kHalfPi,-kHalfPi,kPi,-kPi,1.5*kPi,-1.5*kPi}) {
        const auto region=historyRevolutionRegion(true,onAxis);
        RevolutionProvenance origins;
        const Body body=makeRevolution(frame,region,angle,&origins);
        checkRevolutionSources(body,origins,frame,{region.outer,region.holes.front()},angle);
        FK_CHECK(writeBodyBinary(body)==writeBodyBinary(makeRevolution(frame,region,angle)));
    }
}

FK_TEST(HistoryRevolutionCurvesSheetsAndRejectedOutputs) {
    ProfileRegion torus;
    torus.outer.segments={{std::make_shared<Circle<2>>(Vec2(4,0),Vec2(1,0),Vec2(0,1),1),{0,kTwoPi},91}};
    for(double angle:{kTwoPi,kHalfPi,-1.5*kPi}) {
        RevolutionProvenance origins;
        const Body body=makeRevolution(Frame3(),torus,angle,&origins);
        checkRevolutionSources(body,origins,Frame3(),{torus.outer},angle);
    }
    const std::vector<ProfileLoop> chains{{},{{historyProfileLine(Vec2(2,0),Vec2(2,3),101),historyProfileLine(Vec2(2,3),Vec2(4,3),102)}}};
    for(double angle:{kTwoPi,kHalfPi,-kHalfPi,1.5*kPi,-1.5*kPi}) {
        RevolutionProvenance origins;
        const Body body=makeSheetRevolution(Frame3(),chains,angle,&origins);
        FK_CHECK(body.isSheet());
        checkRevolutionSources(body,origins,Frame3(),chains,angle,true);
        FK_CHECK(writeBodyBinary(body)==writeBodyBinary(makeSheetRevolution(Frame3(),chains,angle)));
    }
    RevolutionProvenance failed;failed.faces.push_back({});
    FK_CHECK_THROWS(makeRevolution(Frame3(),historyRevolutionRegion(),0,&failed));FK_CHECK(failed.faces.empty());
    failed.faces.push_back({});
    FK_CHECK_THROWS(makeRevolution(Frame3(),{},kTwoPi,&failed));FK_CHECK(failed.faces.empty());
    failed.faces.push_back({});
    FK_CHECK_THROWS(makeSheetRevolution(Frame3(),chains,0,&failed));FK_CHECK(failed.faces.empty());
    ExtrusionProvenance rejectedPrism;rejectedPrism.faces.push_back({});
    FK_CHECK_THROWS(makePrism(Frame3(),{}, {},1,&rejectedPrism));FK_CHECK(rejectedPrism.faces.empty());
}

namespace {
ProfileRegion allHistoryRectangle(double a=2) {
    ProfileRegion r;const Vec2 p[]={Vec2(-a,-a),Vec2(a,-a),Vec2(a,a),Vec2(-a,a)};
    for(int i=0;i<4;++i)r.outer.segments.push_back(historyProfileLine(p[i],p[(i+1)%4],41+i));
    return r;
}
void allHistoryFaces(const Body &body,const BodyHistory &history,const std::vector<const Body *> &operands) {
    FK_CHECK(history.operandCount()==int(operands.size()));
    for(FaceId f:body.faces())FK_CHECK(history.isDeclared(EntityKind::Face,f.index));
    const auto resolved=resolveHistory(operands,body,&history);
    for(FaceId f:body.faces())FK_CHECK(resolved.origins(EntityKind::Face,f.index).size()==history.origins(EntityKind::Face,f.index).size());
}
void allHistoryLabels(const Body &body,const FaceAliases &labels) {
    for(FaceId f:body.faces()){FK_CHECK(std::size_t(f.index)<labels.size());if(std::size_t(f.index)<labels.size())FK_CHECK(!labels[std::size_t(f.index)].empty());}
}
}

FK_TEST(HistorySweepAndLoftKeepSectionSources) {
    const auto region=allHistoryRectangle(0.5);
    const std::vector<PathSegment> path{{std::make_shared<Line<3>>(Vec3(),Vec3(0,0,1)),{0,3}}};
    FaceAliases labels;const auto sweep=sweepRegions(Frame3(),{region},path,SweepOrientation::MinimalTwist,&labels);
    allHistoryLabels(sweep,labels);FK_CHECK(writeBodyBinary(sweep)==writeBodyBinary(sweepRegions(Frame3(),{region},path)));
    for(int source=41;source<=44;++source){bool found=false;for(const auto &cell:labels)for(const auto &label:cell)found=found||label.find(".P"+std::to_string(source)+".")!=std::string::npos;FK_CHECK(found);}
    ProfileRegion round;round.outer.segments={{std::make_shared<Circle<2>>(Vec2(),Vec2(1,0),Vec2(0,1),0.4),{0,kTwoPi},99}};
    auto tube=sweepSheet(Frame3(),{round.outer},path,SweepOrientation::MinimalTwist,&labels);allHistoryLabels(tube,labels);
    for(const auto &cell:labels)for(const auto &label:cell)FK_CHECK(label.find(".P99.")!=std::string::npos);
    std::vector<LoftSection> sections{{Frame3(),region.outer,{}},{Frame3(Vec3(0,0,3),Vec3(0,0,1),Vec3(1,0,0)),reversed(round.outer),{}}};
    for(bool ruled:{false,true}) {
        const auto body=loftSolid(sections,ruled,&labels);allHistoryLabels(body,labels);
        FK_CHECK(writeBodyBinary(body)==writeBodyBinary(loftSolid(sections,ruled)));
        bool circle=false;for(const auto &cell:labels)circle=circle||std::find(cell.begin(),cell.end(),"side.S1.P99")!=cell.end();FK_CHECK(circle);
    }
    labels={{"old"}};FK_CHECK_THROWS(sweepRegions(Frame3(),{},path,SweepOrientation::MinimalTwist,&labels));FK_CHECK(labels.empty());
    labels={{"old"}};FK_CHECK_THROWS(loftSolid({},false,&labels));FK_CHECK(labels.empty());
}

FK_TEST(HistoryPrimitivesAndSurfaceRootsEmitLabels) {
    FaceAliases labels;
    allHistoryLabels(makeBox(Frame3(),2,3,4,&labels),labels);
    const auto cylinder=makeCylinder(Frame3(),2,-3,&labels);allHistoryLabels(cylinder,labels);
    allHistoryLabels(makeSphere(Frame3(),2,&labels),labels);
    allHistoryLabels(makeCone(Frame3(),2,0,3,&labels),labels);
    allHistoryLabels(makeTorus(Frame3(),3,1,&labels),labels);
    const auto region=allHistoryRectangle();allHistoryLabels(planarSheet(Frame3(),{region},&labels),labels);
    std::vector<PathSegment> loop;for(const auto &s:region.outer.segments)loop.push_back({embedCurve(s.curve,Frame3()),s.range});
    const auto boundary=boundarySheet(loop,1e-9,&labels);allHistoryLabels(boundary,labels);
    const auto fill=fillSurface(loop,{}, {},{},nullptr,&labels);allHistoryLabels(fill,labels);
    FK_CHECK(writeBodyBinary(boundary)==writeBodyBinary(boundarySheet(loop)));
    FK_CHECK(writeBodyBinary(fill)==writeBodyBinary(fillSurface(loop,{},{})));
}

FK_TEST(HistoryTransformsDraftAndExtractedEdgesAreExplicit) {
    const auto box=makeBox(Frame3(),6,5,4);BodyHistory history;
    for(const auto &transform:{Transform3::translation(Vec3(10,2,3)),Transform3::reflection(Vec3(),Vec3(1,0,0)),Transform3::scaling(Vec3(),2)}) {
        const auto body=transformBody(box,transform,&history);allHistoryFaces(body,history,{&box});
        for(EdgeId e:body.edges())FK_CHECK(history.isDeclared(EntityKind::Edge,e.index));
        FK_CHECK(writeBodyBinary(body)==writeBodyBinary(transformBody(box,transform)));
    }
    const int right=faceAt(box,Vec3(6,2,2),Vec3(1,0,0));
    const auto draft=draftFaces(box,{FaceId(right)},Vec3(),Vec3(0,0,1),0.1,&history);allHistoryFaces(draft,history,{&box});
    const int top=faceAt(box,Vec3(2,2,4),Vec3(0,0,1));
    const auto sheet=facesAsSheet(box,{FaceId(top)},&history);
    for(EdgeId e:sheet.edges()){const auto &links=history.origins(EntityKind::Edge,e.index);FK_CHECK(history.isDeclared(EntityKind::Edge,e.index)&&links.size()==1);if(links.size()==1)FK_CHECK(sheet.edge(e).curve==box.edge(EdgeId(links.front().index)).curve);}
    ThickenOptions options;options.thickness=0.5;
    for(bool direction:{false,true})for(auto side:{ThickenSide::Forward,ThickenSide::Backward,ThickenSide::Both}) {
        options.useDirection=direction;options.side=side;
        const auto body=thickenSheet(box,{FaceId(top)},options,&history);allHistoryFaces(body,history,{&box});
        int generated=0;for(FaceId f:body.faces())for(const auto &link:history.origins(EntityKind::Face,f.index))generated+=link.kind==EntityKind::Edge&&link.relation==HistoryRelation::Generated;
        FK_CHECK(generated==4);
    }
    options.thickness=0;FK_CHECK_THROWS(thickenSheet(box,{FaceId(top)},options,&history));FK_CHECK(history.operandCount()==0);
}

FK_TEST(HistoryTrimExtendShellAndProjectionComposeOrigins) {
    const auto sheet=makePlaneSheet(Frame3(),5);
    const auto tool=makePlaneSheet(Frame3(Vec3(),Vec3(1,0,0),Vec3(0,1,0)),10);BodyHistory history;
    const auto trimmed=trimSheet(sheet,tool,Vec3(2,0,0),1e-6,&history);allHistoryFaces(trimmed,history,{&sheet,&tool});
    std::vector<BodyHistory> parts;const auto pieces=splitSheet(sheet,tool,{},&parts);FK_CHECK(pieces.size()==parts.size());
    for(std::size_t i=0;i<pieces.size();++i)allHistoryFaces(pieces[i],parts[i],{&sheet,&tool});
    const auto extended=extendSheet(sheet,{sheet.edges().front()},1,false,&history);allHistoryFaces(extended,history,{&sheet});
    const auto box=makeBox(Frame3(),8,6,4);const int top=faceAt(box,Vec3(2,2,4),Vec3(0,0,1));
    const auto shell=shellBody(box,{FaceId(top)},0.5,&history);allHistoryFaces(shell,history,{&box});
    for(FaceId f:shell.faces())FK_CHECK(!history.origins(EntityKind::Face,f.index).empty());
    const auto cylinder=makeCylinder(Frame3(),3,4);const int cap=faceAt(cylinder,Vec3(0,0,4),Vec3(0,0,1));
    const auto cup=shellBody(cylinder,{FaceId(cap)},0.5,&history);allHistoryFaces(cup,history,{&cylinder});
    ProjectedCutReport report;
    const auto profile=allHistoryRectangle(1);
    const auto cut=projectedProfileCut(sheet,Frame3(),{profile},{},Vec3(0,0,1),ProjectedCutMode::RemoveInside,&report,{},&history);
    allHistoryFaces(cut,history,{&sheet});
    const Frame3 outside(Vec3(20,0,5),Vec3(0,0,1),Vec3(1,0,0));
    FK_CHECK_THROWS(projectedProfileCut(sheet,outside,{profile},{},Vec3(0,0,-1),ProjectedCutMode::RemoveInside,&report,{},&history));
    FK_CHECK(history.operandCount()==0);
}

FK_TEST(HistoryTransportDoesNotInventMissingBranches) {
    const auto box=makeBox(Frame3(),2,3,4);auto a=ForgeCad::Kernel::detail::indexHistory(box,box,HistoryRelation::Same);
    auto b=makeHistory({&box},box);auto stage=makeHistory({&box,&box},box);const int face=box.faces().front().index;
    stage.declareOrigins(EntityKind::Face,face,{{0,EntityKind::Face,face,HistoryRelation::Same},{1,EntityKind::Face,face,HistoryRelation::Modified}});
    const auto missing=ForgeCad::Kernel::detail::transportHistory(stage,{&a,&b});FK_CHECK(!missing.isDeclared(EntityKind::Face,face));
    b.declareOrigins(EntityKind::Face,face,{});const auto known=ForgeCad::Kernel::detail::transportHistory(stage,{&a,&b});FK_CHECK(known.isDeclared(EntityKind::Face,face));FK_CHECK(known.origins(EntityKind::Face,face).size()==1);
}
