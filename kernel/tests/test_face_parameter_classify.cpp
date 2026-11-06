#include "fk_test.h"
#include "fk_bspline_surface.h"
#include "fk_classify_uv.h"
#include "fk_body_check.h"
#include "fk_primitives.h"
#include <limits>
using namespace ForgeCad::Kernel;
namespace {
Body boundedPatch(double span, bool hole) {
    auto surface = std::make_shared<BSplineSurface>(1, 1,
        std::vector<double>{0,0,span,span}, std::vector<double>{0,0,span,span}, 2, 2,
        std::vector<Vec3>{{0,0,0},{0,4,0},{10,0,2},{10,4,2}});
    Body::BuildFace face; face.surface = surface;
    std::vector<Vec3> vertices;
    std::vector<Body::BuildEdge> edges;
    const auto loop = [&](const std::vector<Vec2> &uv) {
        std::vector<Body::BuildFin> fins;
        const int base = int(vertices.size());
        for (const auto &p : uv) vertices.push_back(surface->point(p.x(),p.y()));
        for (int i=0;i<int(uv.size());++i) {
            const int j=(i+1)%int(uv.size());
            const Vec3 delta=vertices[base+j]-vertices[base+i];
            const double length=norm(delta);
            Body::BuildEdge edge;edge.start=base+i;edge.end=base+j;
            edge.curve=std::make_shared<Line<3>>(vertices[base+i],delta);edge.range={0,length};
            Body::BuildFin fin;fin.edge=int(edges.size());
            // Le Line normalizzano la direzione: la SP-curve richiede la
            // velocita' UV per lo stesso parametro metrico dell'edge 3D.
            fin.pcurve=std::make_shared<BSplineCurve<2>>(1,std::vector<double>{0,0,length,length},std::vector<Vec2>{uv[i],uv[j]});
            edges.push_back(edge);fins.push_back(fin);
        }
        face.loops.push_back(std::move(fins));
    };
    loop({{0,0},{span,0},{span,span},{0,span}});
    if(hole)loop({{.4*span,.4*span},{.4*span,.6*span},{.6*span,.6*span},{.6*span,.4*span}});
    return Body::buildSheet(vertices,edges,{face});
}
}
FK_TEST(ParameterClassificationUsesPhysicalBoundaryBudget) {
    const double tolerance=1e-7;
    for(double span:{.001,1.,1000.}) {
        const Body body=boundedPatch(span,true);const auto face=body.faces().front();
        FK_CHECK(checkBody(body).empty());
        FaceParameterClassifier classifier(body,face,tolerance);
        const double du=.25*tolerance*span/std::sqrt(104.),dv=.25*tolerance*span/4.;
        for(const Vec2 &uv:std::vector<Vec2>{{-du,.25*span},{span+du,.75*span},{.25*span,-dv},{.75*span,span+dv},{-du,-dv},{span+du,span+dv}}) {
            FK_CHECK(classifier.classify(uv)==PointLocation::Boundary);
            const Vec3 point=body.face(face).surface->point(uv.x(),uv.y());
            FK_CHECK(classifyPointOnFace(body,face,point,tolerance)==PointLocation::Boundary);
        }
        for(const Vec2 &uv:std::vector<Vec2>{{-16*du,.5*span},{span+16*du,.5*span},{.5*span,-16*dv},{.5*span,span+16*dv}})
            FK_CHECK(classifier.classify(uv)==PointLocation::Outside);
        FK_CHECK(classifier.classify({.25*span,.25*span})==PointLocation::Inside);
        FK_CHECK(classifier.classify({.5*span,.5*span})==PointLocation::Outside);
        FK_CHECK(classifier.classify({.4*span,.5*span})==PointLocation::Boundary);
        FK_CHECK(classifier.classify({std::numeric_limits<double>::infinity(),0})==PointLocation::Outside);
        FK_CHECK(classifier.classify({0,std::numeric_limits<double>::quiet_NaN()})==PointLocation::Outside);
    }
}
FK_TEST(ParameterClassificationPreservesPeriodicWraps) {
    const Body body=makeCylinder(Frame3(),3,4);
    for(const auto id:body.faces())if(body.face(id).surface->type()==SurfaceType::Cylinder) {
        FaceParameterClassifier classifier(body,id,1e-7);
        for(int turn:{-3,0,4}) {
            FK_CHECK(classifier.classify({.4+turn*kTwoPi,2})==PointLocation::Inside);
            FK_CHECK(classifier.classify({.4+turn*kTwoPi,4+2e-8})==PointLocation::Boundary);
            FK_CHECK(classifier.classify({.4+turn*kTwoPi,4+4e-7})==PointLocation::Outside);
        }
    }
}
