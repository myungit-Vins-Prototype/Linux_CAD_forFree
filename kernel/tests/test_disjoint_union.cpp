#include "fk_test.h"
#include "fk_boolean.h"
#include "fk_body_check.h"
#include "fk_body_io.h"
#include "fk_bspline_surface.h"
#include "fk_classify.h"
#include "fk_history.h"
#include "fk_mass.h"
#include "fk_pcurve.h"
#include "fk_primitives.h"
#include "fk_surface_algo.h"

#include <array>
#include <stdexcept>

using namespace ForgeCad::Kernel;
namespace {
Body box(Vec3 origin, double x, double y, double z) {
    return makeBox(Frame3(origin,{0,0,1},{1,0,0}),x,y,z);
}
void valid(const Body &body) {
    for (const auto &issue : checkBody(body,{true,true})) fktest::reportFailure(__FILE__,__LINE__,issue.message);
}
void historyValid(const Body &a,const Body &b,const Body &result,const BodyHistory &history) {
    FK_CHECK(history.operandCount()==2);
    bool contributed[2]={false,false};
    for (FaceId face : result.faces()) {
        FK_CHECK(history.isDeclared(EntityKind::Face,face.index));
        for (const auto &link : history.origins(EntityKind::Face,face.index)) {
            FK_CHECK(link.operand==0 || link.operand==1);
            if (link.operand<0 || link.operand>1) continue;
            FK_CHECK((link.operand==0?a:b).contains(FaceId(link.index)));
            contributed[link.operand]=true;
        }
    }
    FK_CHECK(contributed[0] && contributed[1]);
}
void bothOrders(const Body &a,const Body &b,double volume,int shells) {
    for (bool reverse : {false,true}) {
        const auto &first=reverse?b:a, &second=reverse?a:b;
        BodyHistory history;
        const auto united=uniteDisjointBodies(first,second,{},&history);
        valid(united);
        FK_CHECK_NEAR(massProperties(united).volume,volume,1e-8);
        FK_CHECK(int(united.shells().size())==shells);
        historyValid(first,second,united,history);
    }
}

// Cella esatta tra un piano orizzontale e S(u,v)=(u,v,1+0.1*u*v).
// Il contatto e' una vera superficie libera bilineare, non un piano
// rivestito come spline. Le SP-curve del carrier sono isoparametriche esatte.
Body graphCell(const SurfacePtr &carrier,Interval u,Interval v,double flat,bool below) {
    const auto point=[&](double x,double y,bool graph) {return graph?carrier->point(x,y):Vec3(x,y,flat);};
    std::vector<Vec3> points;
    for (bool upper : {false,true}) for (Vec2 p : {Vec2(u.lo,v.lo),Vec2(u.hi,v.lo),Vec2(u.hi,v.hi),Vec2(u.lo,v.hi)})
        points.push_back(point(p.x(),p.y(),upper==below));
    struct EdgeSpec {int a,b;CurvePtr<3> curve;Interval range;CurvePtr<2> graphPCurve;};
    std::vector<EdgeSpec> edges;
    const std::array<std::pair<int,int>,4> pairs{{{0,1},{1,2},{3,2},{0,3}}};
    for (int layer=0;layer<2;++layer) for (int side=0;side<4;++side) {
        const int a=pairs[side].first+4*layer,b=pairs[side].second+4*layer;
        if ((layer==1)==below) {
            const bool horizontal=side%2==0;
            const double fixed=side==0?v.lo:side==2?v.hi:side==1?u.hi:u.lo;
            edges.push_back({a,b,horizontal?carrier->vIso(fixed):carrier->uIso(fixed),horizontal?u:v,
                std::make_shared<Line<2>>(horizontal?Vec2(0,fixed):Vec2(fixed,0),horizontal?Vec2(1,0):Vec2(0,1))});
        } else edges.push_back({a,b,std::make_shared<Line<3>>(points[a],points[b]-points[a]),{0,distance(points[a],points[b])},{}});
    }
    for (int i=0;i<4;++i) edges.push_back({i,i+4,std::make_shared<Line<3>>(points[i],points[i+4]-points[i]),{0,distance(points[i],points[i+4])},{}});
    std::vector<Body::BuildEdge> builtEdges;
    for (const auto &edge : edges) builtEdges.push_back({edge.a,edge.b,edge.curve,edge.range});
    const std::array<std::array<int,4>,6> cycles{{{{0,3,2,1}},{{4,5,6,7}},{{0,1,5,4}},{{1,2,6,5}},{{2,3,7,6}},{{3,0,4,7}}}};
    const std::array<Vec3,6> normals{{{0,0,-1},{0,0,1},{0,-1,0},{1,0,0},{0,1,0},{-1,0,0}}};
    std::vector<Body::BuildFace> faces;
    for (int f=0;f<6;++f) {
        const bool graph=f==(below?1:0);
        Body::BuildFace face;
        face.surface=graph?carrier:std::make_shared<Plane>(Frame3(points[cycles[f][0]],normals[f],f<2?Vec3(1,0,0):Vec3(0,0,1)));
        face.sense=graph?below:true;
        face.loops.emplace_back();
        for (int i=0;i<4;++i) {
            const int a=cycles[f][i],b=cycles[f][(i+1)%4];
            int found=-1;bool sense=false;
            for (int e=0;e<int(edges.size());++e)
                if ((edges[e].a==a&&edges[e].b==b)||(edges[e].a==b&&edges[e].b==a)) {found=e;sense=edges[e].a==a;break;}
            if (found<0) throw std::logic_error("fixture graphCell: edge mancante");
            face.loops.back().push_back({found,sense,graph?edges[found].graphPCurve:CurvePtr<2>{}});
        }
        faces.push_back(std::move(face));
    }
    Body result=Body::build(points,builtEdges,faces);
    if (computePCurves(result)!=0) throw std::logic_error("fixture graphCell: SP-curve mancanti");
    valid(result);
    return result;
}
SurfacePtr carrier() {
    return std::make_shared<BSplineSurface>(1,1,std::vector<double>{0,0,2,2},std::vector<double>{0,0,2,2},2,2,
        std::vector<Vec3>{{0,0,1},{0,2,1},{2,0,1},{2,2,1.4}});
}
}

FK_TEST(DisjointUnionFullAndPartialPlanarContact) {
    const Body a=box({},1,1,1);
    bothOrders(a,box({1,0,0},1,1,1),2,1);
    bothOrders(a,box({1,.25,.25},.5,.5,.5),1.125,1);
    const auto united=uniteDisjointBodies(a,box({1,.25,.25},.5,.5,.5));
    SolidClassifier classifier(united,kLinearResolution);
    FK_CHECK(classifier.classify({1.25,.5,.5})==PointLocation::Inside);
    FK_CHECK(classifier.classify({1.25,.1,.5})==PointLocation::Outside);
}

FK_TEST(DisjointUnionContactAcrossTwoTargetFaces) {
    BooleanOptions preserve;preserve.unifySameDomain=false;
    const Body a=box({},1,2,1), one=box({1,0,0},1,1,1), two=box({1,1,0},1,1,1);
    const Body target=booleanOperation(one,two,BooleanOperation::Unite,preserve);
    valid(target);
    bothOrders(a,target,4,1);
}

FK_TEST(DisjointUnionBSplinePartialContactCopiedAndCodec) {
    const auto original=carrier();
    const auto copied=std::make_shared<BSplineSurface>(static_cast<const BSplineSurface &>(*original));
    FK_CHECK(original.get()!=copied.get() && sameSurfaceParameterization(*original,*copied));
    const Body below=graphCell(original,{0,2},{0,2},0,true);
    const Body above=graphCell(copied,{.5,1.5},{.5,1.5},3,false);
    FK_CHECK_NEAR(massProperties(below).volume,4.4,1e-9);
    FK_CHECK_NEAR(massProperties(above).volume,1.9,1e-9);
    bothOrders(below,above,6.3,1);
    const auto restoredBelow=readBodyBinary(writeBodyBinary(below)), restoredAbove=readBodyBinary(writeBodyBinary(above));
    bothOrders(restoredBelow,restoredAbove,6.3,1);
}

FK_TEST(DisjointUnionPreservesCavity) {
    const auto outer=box({},4,4,4), cavity=box({1,1,1},2,2,2);
    const auto hollow=booleanOperation(outer,cavity,BooleanOperation::Subtract);
    FK_CHECK(hollow.shells().size()==2);
    const auto plug=box({1.5,1.5,1},1,1,1);
    bothOrders(hollow,plug,57,2);
    const auto result=uniteDisjointBodies(hollow,plug);
    SolidClassifier classifier(result,kLinearResolution);
    FK_CHECK(classifier.classify({1.25,1.25,2})==PointLocation::Outside);
    FK_CHECK(classifier.classify({2,2,1.5})==PointLocation::Inside);
    FK_CHECK(classifier.classify({.5,.5,.5})==PointLocation::Inside);
}

FK_TEST(DisjointUnionEdgesPointsAndSeparatedRemainComponents) {
    const auto a=box({},1,1,1);
    bothOrders(a,box({1,1,0},1,1,1),2,2);
    bothOrders(a,box({1,1,1},1,1,1),2,2);
    bothOrders(a,box({2,0,0},1,1,1),2,2);
}

FK_TEST(DisjointUnionDifferenceProvenanceAndMultipleComponents) {
    const auto prism=box({},2,2,6), limit=box({-1,-1,3},4,4,2);
    const auto disjoint=booleanOperation(prism,limit,BooleanOperation::Subtract);
    FK_CHECK(disjoint.shells().size()==2);
    bothOrders(disjoint,limit,48,1);
}

FK_TEST(DisjointUnionObservedInvalidPreconditionsAndGenericOverlap) {
    const auto a=box({},2,2,2), identical=readBodyBinary(writeBodyBinary(a)), contained=box({.5,.5,.5},1,1,1);
    FK_CHECK_THROWS(uniteDisjointBodies(a,identical));
    FK_CHECK_THROWS(uniteDisjointBodies(a,contained));
    FK_CHECK_THROWS(uniteDisjointBodies(contained,a));
    // Questa API non dimostra la precondizione per operandi arbitrari:
    // nessun test pretende di rilevare ogni possibile sovrapposizione.
    // L'unione generale DEVE continuare a gestire gli interni sovrapposti.
    const auto overlapping=box({1,0,0},2,2,2);
    for (bool reverse : {false,true}) {
        const auto &first=reverse?overlapping:a,&second=reverse?a:overlapping;
        BodyHistory history;
        const auto result=booleanOperation(first,second,BooleanOperation::Unite,{},&history);
        valid(result);FK_CHECK_NEAR(massProperties(result).volume,12,1e-9);
        FK_CHECK(result.shells().size()==1);historyValid(first,second,result,history);
    }
}
