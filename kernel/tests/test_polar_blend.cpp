#include "fk_test.h"
#include "fk_blend.h"
#include "fk_body_check.h"
#include "fk_body_io.h"
#include "fk_bspline_surface.h"
#include "fk_history.h"
#include "fk_surface_algo.h"
#include "fk_tessellate.h"
#include "fk_transform.h"
#include <algorithm>

using namespace ForgeCad::Kernel;
namespace {
Body closedPolarFan(double bulge = 0., bool smoothBottom = true) {
    const int count = 18;
    const std::vector<double> uk{0,0,0,0,0,1,1,1,1,1}, vk{0,0,0,1,1,1};
    const std::vector<Vec3> profile{{0,0,15},{6,0,13.5},{8,0,smoothBottom ? 6.5 : 7.5},{6,0,smoothBottom ? 0. : 1.5},{0,0,0}};
    const auto uv = [](double v) { return std::make_shared<BSplineCurve<2>>(1,
        std::vector<double>{0,0,1,1},std::vector<Vec2>{{0,v},{1,v}}); };
    std::vector<Body::BuildEdge> edges;
    std::vector<Body::BuildFace> faces;
    const auto left = Transform3::rotation({},{0,0,1},-kPi/count);
    const auto right = Transform3::rotation({},{0,0,1},kPi/count);
    std::vector<Vec3> poles;
    for (int i = 0; i < 5; ++i) {
        const auto a = left.applyToPoint(profile[i]), b = right.applyToPoint(profile[i]);
        poles.push_back(a); poles.push_back(.5*(a+b)+(i==2 ? Vec3{bulge,0,0} : Vec3{})); poles.push_back(b);
    }
    const auto base = std::make_shared<BSplineSurface>(4,2,uk,vk,5,3,poles);
    for (int i = 0; i < count; ++i) {
        const auto surface = transformSurface(base,Transform3::rotation({},{0,0,1},i*kTwoPi/count));
        edges.push_back({0,1,surface->vIso(0),{0,1}});
        faces.push_back({surface,true,{{{i,true,uv(0)},{(i+1)%count,false,uv(1)}}}});
    }
    return Body::build({profile.front(),profile.back()},edges,faces);
}
void validateBlend(const Body &base, const Body &result, const BodyHistory &history, double radius) {
    FK_CHECK(result.shells().size() == 1 && !result.isSheet());
    FK_CHECK(checkBody(result,{true,true}).empty());
    for (int k = 0; k < 3; ++k) for (int i = 0; i < history.resultSize(EntityKind(k)); ++i)
        FK_CHECK(history.isDeclared(EntityKind(k),i));
    for (auto f : result.faces()) {
        const auto &links = history.origins(EntityKind::Face,f.index);
        if (links.size() != 1 || links.front().kind != EntityKind::Edge) continue;
        const auto &s = *result.face(f).surface;
        const auto edge = base.edge(EdgeId(links.front().index));
        const auto &a = *base.face(base.finFace(edge.forward)).surface;
        const auto &b = *base.face(base.finFace(edge.backward)).surface;
        for (double fraction : {.2,.5,.8}) {
            const double u = s.uDomain().lo+fraction*s.uDomain().length();
            const auto pa = s.point(u,0), pb = s.point(u,1);
            const auto qa = projectPoint(a,pa), qb = projectPoint(b,pb);
            FK_CHECK(qa.distance < 1e-7 && qb.distance < 1e-7);
            const auto na = a.normal(qa.u,qa.v), nb = b.normal(qb.u,qb.v);
            FK_CHECK(std::abs(dot(s.normal(u,0),na)) > 1-1e-8);
            FK_CHECK(std::abs(dot(s.normal(u,1),nb)) > 1-1e-8);
            const double sign = distance(pa+radius*na,pb+radius*nb) < distance(pa-radius*na,pb-radius*nb) ? 1 : -1;
            const auto center = pa+sign*radius*na;
            FK_CHECK(distance(center,pb+sign*radius*nb) < 1e-7);
            for (double v : {0.,.25,.5,.75,1.}) FK_CHECK_NEAR(distance(s.point(u,v),center),radius,1e-7);
        }
    }
    TessellationOptions options; options.deflection = .03; options.threads = 2;
    const auto mesh = tessellate(result,options);
    FK_CHECK(mesh.failedFaces == 0 && mesh.faces.size() == result.faces().size());
    FK_CHECK(checkBody(readBodyBinary(writeBodyBinary(result)),{true,true}).empty());
}
}

FK_TEST(PolarBlendPositiveRadiusAtEighteenWayPoles) {
    for (double bulge : {0.,4.}) {
        const auto body = closedPolarFan(bulge);
        const auto before = writeBodyBinary(body);
        FK_CHECK(checkBody(body).empty());
        BodyHistory history;
        const auto result = blendEdges(body,body.edges(),.03,false,&history,false);
        FK_CHECK(result.faces().size() == (bulge == 0 ? 37 : 73));
        validateBlend(body,result,history,.03);
        FK_CHECK(writeBodyBinary(body) == before);
        FK_CHECK_THROWS(blendEdges(body,{body.edges().front()},.03,false,nullptr,false));
        FK_CHECK_THROWS(blendEdges(body,body.edges(),20.,false,nullptr,false));
        FK_CHECK(writeBodyBinary(body) == before);
    }
}

FK_TEST(PolarBlendPoseScaleAndReversedEdgeDirections) {
    for (double scale : {.25,2.5}) {
        const auto transform = Transform3::translation({17,-23,11})
            *Transform3::rotation({},{1,2,3},.42)*Transform3::scaling({},scale);
        auto body = transformBody(closedPolarFan(),transform);
        body.reverseEdge(body.edges()[3]); body.reverseEdge(body.edges()[11]);
        const auto before = writeBodyBinary(body);
        auto edges = body.edges(); std::reverse(edges.begin(),edges.end());
        BodyHistory history;
        const auto result = blendEdges(body,edges,.03*scale,false,&history,true);
        FK_CHECK(result.faces().size() == 37 && checkBody(result,{true,true}).empty());
        for (int k = 0; k < 3; ++k) for (int i = 0; i < history.resultSize(EntityKind(k)); ++i)
            FK_CHECK(history.isDeclared(EntityKind(k),i));
        FK_CHECK(writeBodyBinary(body) == before);
    }
}

FK_TEST(PolarBlendTwoCommonSphericalCaps) {
    const auto body = closedPolarFan(0.,false);
    FK_CHECK(checkBody(body).empty());
    BodyHistory history;
    const auto result = blendEdges(body,body.edges(),.3,false,&history,false);
    FK_CHECK(result.faces().size() == 38 && result.vertices().size() == 36);
    validateBlend(body,result,history,.3);
}
