#include "fk_test.h"
#include "fk_draft.h"
#include "fk_sew.h"
#include "fk_blend.h"
#include "fk_primitives.h"
#include "fk_precision.h"
#include "fk_mass.h"
#include "fk_body_check.h"
#include "fk_body_io.h"
#include "fk_tessellate.h"
#include "fk_transform.h"
using namespace ForgeCad::Kernel;

namespace {
std::vector<FaceId> sides(const Body &body) {
    std::vector<FaceId> faces;
    for (FaceId f : body.faces())
        if (std::fabs(static_cast<const Plane &>(*body.face(f).surface).frame().zDir().z()) < 0.1) faces.push_back(f);
    return faces;
}
}
FK_TEST(DraftBoxNeutralPlane) {
    const Body box = makeBox(Frame3(), 20, 10, 5);
    const auto bytes = writeBodyBinary(box);
    const auto faces = sides(box);
    FK_CHECK(faces.size() == 4);
    for (double sign : {-1.0, 1.0}) {
        const double angle = sign * 5 * kPi / 180.0, t = std::tan(angle), h = 5;
        const Body result = draftFaces(box, faces, Vec3(), Vec3(0,0,1), angle);
        FK_CHECK(checkBody(result, {true,true}).empty());
        FK_CHECK_NEAR(massProperties(result).volume, 200*h - 30*t*h*h + 4*t*t*h*h*h/3, 1e-7);
        for (VertexId v : box.vertices()) {
            const Vec3 old = box.vertex(v).point, now = result.vertex(v).point;
            if (std::fabs(old.z()) < 1e-8) FK_CHECK(distance(old,now) < 1e-8);
        }
        FK_CHECK(tessellate(result, {}).failedFaces == 0);
        FK_CHECK(result.faces().size() == box.faces().size());
    }
    FK_CHECK(writeBodyBinary(box) == bytes);
    // Piano neutro superiore e direzione invertita.
    const Body reverse = draftFaces(box, faces, Vec3(0,0,5), Vec3(0,0,-1), 5*kPi/180);
    for (VertexId v : box.vertices()) if (box.vertex(v).point.z() == 5) FK_CHECK(distance(box.vertex(v).point, reverse.vertex(v).point) < 1e-8);
    // Un'unica parete cambia il volume di un cuneo esatto.
    const Body one = draftFaces(box, {faces.front()}, Vec3(), Vec3(0,0,1), 0.1);
    const Vec3 n = static_cast<const Plane &>(*box.face(faces.front()).surface).frame().zDir();
    const double width = std::fabs(n.x()) > 0.5 ? 10 : 20;
    FK_CHECK_NEAR(massProperties(one).volume, 1000 - width*25*std::tan(0.1)/2, 1e-7);
}
FK_TEST(DraftRotatedAndInvalid) {
    const Body box = makeBox(Frame3(),20,10,5);
    const auto faces = sides(box);
    const Transform3 move = Transform3::translation(Vec3(12,-7,31)) * Transform3::rotation(Vec3(),Vec3(1,1,0),0.7);
    const Body transformed = transformBody(box,move);
    const Body drafted = draftFaces(transformed,faces,move.applyToPoint(Vec3()),move.applyToVector(Vec3(0,0,1)),0.1);
    const Body expected = transformBody(draftFaces(box,faces,Vec3(),Vec3(0,0,1),0.1),move);
    for (VertexId v : expected.vertices()) FK_CHECK(distance(expected.vertex(v).point,drafted.vertex(v).point)<1e-7);
    FK_CHECK_THROWS(draftFaces(box,{},Vec3(),Vec3(0,0,1),0.1));
    FK_CHECK_THROWS(draftFaces(box,faces,Vec3(),Vec3(),0.1));
    FK_CHECK_THROWS(draftFaces(box,faces,Vec3(),Vec3(0,0,1),0.0));
    FK_CHECK_THROWS(draftFaces(box,faces,Vec3(),Vec3(0,0,1),1.4));
    FK_CHECK_THROWS(draftFaces(box,{FaceId(99999)},Vec3(),Vec3(0,0,1),0.1));
    for (FaceId f : box.faces()) if (std::fabs(static_cast<const Plane &>(*box.face(f).surface).frame().zDir().z())>0.9)
        FK_CHECK_THROWS(draftFaces(box,{f},Vec3(),Vec3(0,0,1),0.1));
    const Body cylinder = makeCylinder(Frame3(),4,5);
    FK_CHECK_THROWS(draftFaces(cylinder,{cylinder.faces().front()},Vec3(),Vec3(0,0,1),0.1));
}

FK_TEST(SheetDraftAndBlend) {
    const Body box = makeBox(Frame3(),20,10,5);
    const auto walls = sides(box);
    for (int count : {1,2,4}) {
        // Una faccia isolata, una coppia cucita, un guscio laterale aperto.
        std::vector<FaceId> selected;
        for (int i = 0; i < count; ++i) selected.push_back(walls[std::size_t(i)]);
        const Body sheet = facesAsSheet(box,selected);
        const Body drafted = draftFaces(sheet,sheet.faces(),Vec3(),Vec3(0,0,1),0.1);
        FK_CHECK(drafted.isSheet());
        FK_CHECK(checkBody(drafted,{true,true}).empty());
        FK_CHECK(tessellate(drafted,{}).failedFaces == 0);
        for (VertexId v : sheet.vertices()) {
            const Vec3 a = sheet.vertex(v).point, b = drafted.vertex(v).point;
            FK_CHECK_NEAR(a.z(),b.z(),1e-8);
            if (std::fabs(a.z()) < 1e-8) FK_CHECK(distance(a,b) < 1e-8);
        }
    }
    const Body sheet = facesAsSheet(box,walls);
    std::vector<EdgeId> seams;
    for (EdgeId e : sheet.edges()) if (!sheet.isLaminar(e)) seams.push_back(e);
    FK_CHECK(seams.size() == 4);
    for (bool chamfer : {false,true}) {
        const Body result = blendEdges(sheet,{seams.front()},0.5,chamfer);
        FK_CHECK(result.isSheet());
        FK_CHECK(checkBody(result,{true,true}).empty());
        FK_CHECK(result.faces().size() == sheet.faces().size()+1);
        FK_CHECK(tessellate(result,{}).failedFaces == 0);
        int free = 0;
        for (EdgeId e : result.edges()) if (result.isLaminar(e)) ++free;
        FK_CHECK(free > 0);
        const Body all = blendEdges(sheet,seams,0.5,chamfer);
        FK_CHECK(all.isSheet() && checkBody(all,{true,true}).empty());
        FK_CHECK(tessellate(all,{}).failedFaces == 0);
    }
    const Body drafted = draftFaces(sheet,sheet.faces(),Vec3(),Vec3(0,0,1),0.1);
    for (bool chamfer : {false,true}) {
        const Body result = blendEdges(drafted,{seams.front()},0.3,chamfer);
        FK_CHECK(result.isSheet() && checkBody(result,{true,true}).empty());
        FK_CHECK(tessellate(result,{}).failedFaces == 0);
    }
    FK_CHECK_THROWS(blendEdges(sheet,{seams.front()},100,false));
    for (EdgeId e : sheet.edges()) if (sheet.isLaminar(e)) {
        FK_CHECK_THROWS(blendEdges(sheet,{e},0.5,false)); break;
    }
}
