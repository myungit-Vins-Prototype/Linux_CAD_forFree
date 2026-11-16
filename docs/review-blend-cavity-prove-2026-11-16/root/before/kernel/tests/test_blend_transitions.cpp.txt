#include "fk_test.h"
#include "fk_blend.h"
#include "fk_blend_contour.h"
#include "fk_blend_loop.h"
#include "fk_body_io.h"
#include "fk_body_check.h"
#include "fk_bspline_surface.h"
#include "fk_curve_ops.h"
#include "fk_extrude.h"
#include "fk_history.h"
#include "fk_tessellate.h"
#include "fk_transform.h"
#include <fstream>
#include <algorithm>
#include <set>
using namespace ForgeCad::Kernel;
namespace {
Body opposedSplits() {
    std::ifstream file(std::string(FORGECAD_SOURCE_DIR) + "/kernel/tests/data/blend_opposed_splits.body",std::ios::binary);
    if (!file) throw std::runtime_error("fixture dei passaggi tangenti mancante");
    return readBodyBinary(std::string(std::istreambuf_iterator<char>(file),{}));
}
void checkRadius(const Body &body, const BodyHistory &history, double radius) {
    int generated = 0;
    for (FaceId face : body.faces()) {
        FK_CHECK(history.isDeclared(EntityKind::Face,face.index));
        bool blend=false;
        for (const auto &origin:history.origins(EntityKind::Face,face.index))
            blend=blend || origin.relation==HistoryRelation::Generated;
        if (!blend) continue;
        ++generated;
        const auto *surface=dynamic_cast<const BSplineSurface *>(body.face(face).surface.get());
        FK_CHECK(surface && surface->vDegree()==2);
        if (!surface) continue;
        for(double fraction:{.2,.5,.8}) {
            const double u=surface->uDomain().lo+fraction*surface->uDomain().length();
            const Vec3 p=surface->point(u,0),a=surface->point(u,.5)-p,b=surface->point(u,1)-p,n=cross(a,b);
            FK_CHECK(norm(n)>0);
            const Vec3 center=p+(squaredNorm(a)*cross(b,n)+squaredNorm(b)*cross(n,a))/(2*squaredNorm(n));
            FK_CHECK_NEAR(distance(center,p),radius,1e-7);
            for(double v:{.25,.75,1.})FK_CHECK_NEAR(distance(center,surface->point(u,v)),radius,1e-7);
        }
    }
    FK_CHECK(generated>0);
}
}
FK_TEST(BlendOpposedSplitsAllSeedsAndWrap) {
    const Body body=opposedSplits();
    const auto before=writeBodyBinary(body);
    for(int seed:{10,11,12,13,14,23,24,25,26,27}) {
        BodyHistory history;
        const auto result=blendEdges(body,{EdgeId(seed)},2.,false,&history);
        FK_CHECK(result.faces().size()==28);
        FK_CHECK(checkBody(result,{true,true}).empty());
        FK_CHECK(checkBody(readBodyBinary(writeBodyBinary(result)),{true,true}).empty());
        checkRadius(result,history,2.);
    }
    FK_CHECK(writeBodyBinary(body)==before);
}
FK_TEST(BlendOpposedSplitsMirrorScalePoseAndRadius) {
    const Body source=opposedSplits();
    for(int mode:{0,1,2,3}) {
        BodyHistory transform;
        const auto motion= mode==0 ? Transform3() : mode==1 ? Transform3::reflection(Vec3(),Vec3(1,0,0)) :
            mode==2 ? Transform3::scaling(Vec3(3,2,1),2.5) :
            Transform3::translation(Vec3(21,-17,9))*Transform3::rotation(Vec3(),normalized(Vec3(1,2,3)),.37);
        const Body body=transformBody(source,motion,&transform);
        std::vector<EdgeId> selected;
        for(int e:{10,11,26,27})for(int d:transform.descendants(EntityKind::Edge,0,e))selected.emplace_back(d);
        if(mode%2)std::reverse(selected.begin(),selected.end());
        const double radius=mode==0?1.5:mode==2?5.:2.;
        BodyHistory history;
        const auto result=blendEdges(body,selected,radius,false,&history);
        FK_CHECK(checkBody(result,{true,true}).empty());
        checkRadius(result,history,radius);
        TessellationOptions options;options.deflection=.1;options.threads=2;
        FK_CHECK(tessellate(result,options).failedFaces==0);
    }
}
FK_TEST(BlendClosedPlanarTangentContourUsesLocalTrims) {
    ProfileRegion region;
    const auto line=[](Vec2 a,Vec2 b){return ProfileSegment{std::make_shared<Line<2>>(a,b-a),{0.,distance(a,b)}};};
    region.outer.segments={line(Vec2(-10,-5),Vec2(10,-5)),
        {std::make_shared<Circle<2>>(makeCircle(Vec2(10,0),5)),{-kHalfPi,kHalfPi}},
        line(Vec2(10,5),Vec2(-10,5)),
        {std::make_shared<Circle<2>>(makeCircle(Vec2(-10,0),5)),{kHalfPi,3*kHalfPi}}};
    const Body body=makeExtrusion(Frame3(),region,10.);
    const auto before=writeBodyBinary(body);
    BodyHistory history;
    const auto result=blendEdges(body,{nearestEdge(body,Vec3(15,0,10),1e-7)},1.,false,&history);
    FK_CHECK(checkBody(result,{true,true}).empty());
    for(auto edge:result.edges())FK_CHECK(!result.isLaminar(edge));
    checkRadius(result,history,1.);
    FK_CHECK(writeBodyBinary(body)==before);
}
