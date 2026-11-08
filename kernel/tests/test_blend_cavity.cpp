#include "fk_test.h"
#include "fk_blend.h"
#include "fk_blend_contour.h"
#include "fk_blend_mitre.h"
#include "fk_blend_section_curve.h"
#include "fk_body_io.h"
#include "fk_body_check.h"
#include "fk_bspline.h"
#include "fk_history.h"
#include "fk_tessellate.h"
#include <algorithm>
#include <fstream>
#include <iterator>
#include <set>
using namespace ForgeCad::Kernel;
namespace {
std::set<int> ids(const std::vector<EdgeId> &edges) {
    std::set<int> result;
    for (auto e : edges) result.insert(e.index);
    return result;
}
Body cavity() {
    std::ifstream file(std::string(FORGECAD_SOURCE_DIR) + "/kernel/tests/data/blend_cavity_loft.body", std::ios::binary);
    if (!file) throw std::runtime_error("fixture cavita' mancante");
    return readBodyBinary(std::string(std::istreambuf_iterator<char>(file), {}));
}
}
// Otto facce reali, isolate dal resto del modello. Due appoggi cambiano
// normale di 0,0019/0,0032 rad: non vanno trasformati in cuciture G1.
FK_TEST(BlendCavityContourIsClosedAndIndependentOfSeed) {
    const auto body = cavity();
    const std::set<int> expected{3,10,11,12,17,18,19,25};
    for (int seed : expected) FK_CHECK(ids(blendCavityContour(body, EdgeId(seed))) == expected);
    for (auto e : body.edges()) if (body.isLaminar(e)) FK_CHECK(blendCavityContour(body,e).empty());
    FK_CHECK(blendCavityContour(body,EdgeId()).empty());
}
FK_TEST(BlendCavityWholeRimKeepsRadiusHistoryAndClosedTrims) {
    const auto body = cavity();
    const auto before = writeBodyBinary(body);
    auto contour = blendCavityContour(body,EdgeId(10));
    for (double radius : {0.5,1.1,1.675}) {
        std::reverse(contour.begin(),contour.end());
        BodyHistory history;
        const auto result = blendEdges(body,contour,radius,false,&history,radius>1.5);
        FK_CHECK(result.faces().size()==18);
        FK_CHECK(checkBody(result,{true,true}).empty());
        FK_CHECK(writeBodyBinary(body)==before);
        FK_CHECK(checkBody(readBodyBinary(writeBodyBinary(result)),{true,true}).empty());
        int generated = 0;
        for (auto f : result.faces()) {
            FK_CHECK(history.isDeclared(EntityKind::Face,f.index));
            bool blend = false;
            for (const auto &link : history.origins(EntityKind::Face,f.index))
                blend = blend || (link.kind==EntityKind::Edge && link.relation==HistoryRelation::Generated);
            if (!blend) continue;
            ++generated;
            const auto *surface = dynamic_cast<const BSplineSurface *>(result.face(f).surface.get());
            FK_CHECK(surface && surface->vDegree()==2);
            if (!surface) continue;
            for (double fraction : {.2,.5,.8}) {
                const double u = surface->uDomain().lo + fraction*surface->uDomain().length();
                const Vec3 p=surface->point(u,0), q=surface->point(u,.5), r=surface->point(u,1);
                const Vec3 a=q-p,b=r-p,n=cross(a,b);
                FK_CHECK(norm(n)>0);
                const Vec3 center=p+(squaredNorm(a)*cross(b,n)+squaredNorm(b)*cross(n,a))/(2*squaredNorm(n));
                FK_CHECK_NEAR(distance(center,p),radius,1e-7);
                for (double v : {.25,.75}) FK_CHECK_NEAR(distance(center,surface->point(u,v)),radius,1e-7);
            }
        }
        FK_CHECK(generated==10);
        TessellationOptions options; options.deflection=.05; options.threads=2;
        FK_CHECK(tessellate(result,options).failedFaces==0);
    }
}
FK_TEST(BlendMitreReverseSupportsPreservesRationalSurface) {
    const auto body=cavity();
    for (auto f:body.faces()) if(auto *s=dynamic_cast<const BSplineSurface *>(body.face(f).surface.get())) {
        const auto copy=std::make_shared<BSplineSurface>(*s);
        const auto reversed=detail::orientBlendMitre(copy,true);
        for(double u:{.2,.8})for(double v:{.1,.9}) {
            const double t=s->uDomain().lo+u*s->uDomain().length();
            const double w=s->vDomain().lo+v*s->vDomain().length();
            FK_CHECK(distance(s->point(t,w),reversed->point(t,s->vDomain().lo+s->vDomain().hi-w))<1e-10);
        }
    }
}
FK_TEST(BlendSectionExtensionKeepsOriginalGeometry) {
    const auto body=cavity();
    const auto &edge=body.edge(EdgeId(10));
    const auto curve=detail::blendSectionCurve(edge.curve,edge.range,50.);
    FK_CHECK(curve->domain().lo<edge.range.lo && curve->domain().hi>edge.range.hi);
    for(int i=0;i<=100;++i) {
        const double t=edge.range.lo+edge.range.length()*i/100.;
        FK_CHECK(distance(curve->point(t),edge.curve->point(t))<1e-7);
    }
}

FK_TEST(BlendCavityRejectsConsumedSideFacesBeforeDocument) {
    const auto body=cavity();
    const auto contour=blendCavityContour(body,EdgeId(10));
    FK_CHECK_THROWS(blendEdges(body,contour,2.1,false,nullptr,false));
}
