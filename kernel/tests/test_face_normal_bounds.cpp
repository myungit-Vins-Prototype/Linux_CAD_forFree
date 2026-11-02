#include "fk_test.h"
#include "fk_face_normal_bounds.h"
#include "fk_bspline_surface.h"
#include "fk_precision.h"

#include <algorithm>
#include <limits>

using namespace ForgeCad::Kernel;
namespace {
SurfacePtr folded() {
    std::vector<Vec3> poles;
    for (int i=0;i<3;++i) for (double v : {0.,1.}) poles.push_back({i == 1 ? 1. : 0.,v,double(i)});
    return std::make_shared<BSplineSurface>(1,1,std::vector<double>{0,0,.5,1,1},std::vector<double>{0,0,1,1},3,2,poles);
}
SurfacePtr graph(std::vector<double> weights = {}) {
    return std::make_shared<BSplineSurface>(1,1,std::vector<double>{0,0,1,1},std::vector<double>{0,0,1,1},2,2,
        std::vector<Vec3>{{0,0,0},{0,1,0},{1,0,.2},{1,1,.4}},std::move(weights));
}
struct Rectangle { Interval u,v; bool reverse = false; };
Body rectangles(const SurfacePtr &surface, const std::vector<Rectangle> &rects, bool sense = true, bool wideSplines = false) {
    std::vector<Vec3> points;
    std::vector<Body::BuildEdge> edges;
    Body::BuildFace face; face.surface = surface; face.sense = sense;
    for (const auto &rect : rects) {
        const auto u = rect.u, v = rect.v;
        const int base = int(points.size()), ebase = int(edges.size());
        points.insert(points.end(),{surface->point(u.lo,v.lo),surface->point(u.hi,v.lo),surface->point(u.hi,v.hi),surface->point(u.lo,v.hi)});
        edges.push_back({base,base+1,surface->vIso(v.lo),u});
        edges.push_back({base+1,base+2,surface->uIso(u.hi),v});
        edges.push_back({base+3,base+2,surface->vIso(v.hi),u});
        edges.push_back({base,base+3,surface->uIso(u.lo),v});
        const auto pc = [&](bool horizontal, double fixed) -> CurvePtr<2> {
            if (wideSplines) return std::make_shared<BSplineCurve<2>>(1,std::vector<double>{-10,-10,10,10},
                horizontal ? std::vector<Vec2>{{-10,fixed},{10,fixed}} : std::vector<Vec2>{{fixed,-10},{fixed,10}});
            return std::make_shared<Line<2>>(horizontal ? Vec2(0,fixed) : Vec2(fixed,0),horizontal ? Vec2(1,0) : Vec2(0,1));
        };
        std::vector<Body::BuildFin> loop{{ebase,true,pc(true,v.lo)}, {ebase+1,true,pc(false,u.hi)},
                                         {ebase+2,false,pc(true,v.hi)}, {ebase+3,false,pc(false,u.lo)}};
        if (rect.reverse != !sense) {
            std::reverse(loop.begin(),loop.end());
            for (auto &fin : loop) fin.sense = !fin.sense;
        }
        face.loops.push_back(std::move(loop));
    }
    return Body::buildSheet(points,edges,{face});
}
Body rectangle(const SurfacePtr &s, Interval u = {.1,.4}, Interval v = {.1,.9}, bool sense = true, bool wide = false) {
    return rectangles(s,{{u,v,false}},sense,wide);
}
FaceId first(const Body &b) { return b.faces().front(); }
class UnknownPCurve final : public Curve<2> {
    CurvePtr<2> basis_;
public:
    explicit UnknownPCurve(CurvePtr<2> basis) : basis_(std::move(basis)) {}
    CurveType type() const override { return CurveType::Other; }
    Interval domain() const override { return basis_->domain(); }
    void evaluate(double t, int order, Vec2 *out) const override { basis_->evaluate(t,order,out); }
};
}

FK_TEST(FaceNormalTrimmedSupportAndNegativeInside) {
    const auto support = folded();
    const auto positive = rectangle(support);
    FK_CHECK(faceHasNonnegativeNormalProjection(positive,first(positive),{0,0,1}));
    const auto negative = rectangle(support,{.6,.9});
    FK_CHECK(!faceHasNonnegativeNormalProjection(negative,first(negative),{0,0,1}));
    const auto crossing = rectangle(support,{.4,.6});
    FK_CHECK(!faceHasNonnegativeNormalProjection(crossing,first(crossing),{0,0,1}));
    const auto reversed = rectangle(support,{.1,.4},{.1,.9},false);
    FK_CHECK(!faceHasNonnegativeNormalProjection(reversed,first(reversed),{0,0,1}));
    FK_CHECK(faceHasNonnegativeNormalProjection(reversed,first(reversed),{0,0,-1}));
    FK_CHECK(faceHasNonnegativeNormalProjection(positive,first(positive),{0,0,123}));
}

FK_TEST(FaceNormalPCurveRestrictedToEdgeRange) {
    const auto body = rectangle(folded(),{.1,.4},{.1,.9},true,true);
    for (auto fi : body.fins()) {
        const auto &s = static_cast<const BSplineCurve<2> &>(*body.fin(fi).pcurve);
        FK_CHECK(s.domain().lo == -10 && s.domain().hi == 10);
    }
    FK_CHECK(faceHasNonnegativeNormalProjection(body,first(body),{0,0,1}));
}

FK_TEST(FaceNormalMissingUnknownAndOutOfDomainPCurves) {
    for (int kind=0;kind<4;++kind) {
        auto body = rectangle(folded());
        const auto fi = body.fins().front(); auto &fin = body.fin(fi);
        if (kind == 0) fin.pcurve.reset();
        if (kind == 1) fin.pcurve = std::make_shared<UnknownPCurve>(fin.pcurve);
        if (kind == 2) fin.pcurve = std::make_shared<TrimmedCurve<2>>(fin.pcurve,.2,.4);
        if (kind == 3) fin.pcurve = std::make_shared<BSplineCurve<2>>(1,std::vector<double>{.2,.2,.4,.4},
            std::vector<Vec2>{{.2,.1},{.4,.1}});
        FK_CHECK(!faceHasNonnegativeNormalProjection(body,first(body),{0,0,1}));
    }
    auto allPositive = rectangle(graph()); allPositive.fin(allPositive.fins().front()).pcurve.reset();
    FK_CHECK(faceHasNonnegativeNormalProjection(allPositive,first(allPositive),{0,0,1}));
}

FK_TEST(FaceNormalHolesComplementAndIsolatedVertex) {
    const auto withHole = rectangles(folded(),{{{.1,.4},{.1,.9},false},{{.2,.3},{.3,.6},true}});
    FK_CHECK(faceHasNonnegativeNormalProjection(withHole,first(withHole),{0,0,1}));
    const auto complement = rectangles(folded(),{{{.1,.4},{.1,.9},true}});
    FK_CHECK(!faceHasNonnegativeNormalProjection(complement,first(complement),{0,0,1}));
    Body::BuildFace isolatedFace; isolatedFace.surface = folded();
    const auto isolated = Body::buildSheet({}, {}, {isolatedFace});
    FK_CHECK(!faceHasNonnegativeNormalProjection(isolated,first(isolated),{0,0,1}));
}

FK_TEST(FaceNormalRationalWeightsAndInvalidInput) {
    for (const auto &weights : {std::vector<double>{},std::vector<double>{2,2,2,2}}) {
        const auto body = rectangle(graph(weights));
        FK_CHECK(faceHasNonnegativeNormalProjection(body,first(body),{0,0,1}));
    }
    const auto rational = rectangle(graph({1,2,1,1}));
    FK_CHECK(!faceHasNonnegativeNormalProjection(rational,first(rational),{0,0,1}));
    const auto body = rectangle(graph());
    FK_CHECK(!faceHasNonnegativeNormalProjection(body,first(body),{}));
    FK_CHECK(!faceHasNonnegativeNormalProjection(body,FaceId(-1),{0,0,1}));
    FK_CHECK(!faceHasNonnegativeNormalProjection(body,first(body),{NAN,0,1}));
    FK_CHECK(!faceHasNonnegativeNormalProjection(body,first(body),{INFINITY,0,1}));
}

FK_TEST(FaceNormalExtrusionSplineGraphAndPeriodicity) {
    const auto curve = std::make_shared<BSplineCurve<3>>(2,std::vector<double>{0,0,0,1,1,1},
        std::vector<Vec3>{{0,0,0},{.5,0,0},{1,0,1}});
    const auto surface = std::make_shared<ExtrusionSurface>(curve,Vec3(0,1,0));
    auto body = rectangle(surface,{.1,.9},{.2,1.2});
    FK_CHECK(faceHasNonnegativeNormalProjection(body,first(body),{0,0,1}));
    FK_CHECK(!faceHasNonnegativeNormalProjection(body,first(body),{0,0,-1}));
    body.fin(body.fins().front()).pcurve.reset();
    FK_CHECK(!faceHasNonnegativeNormalProjection(body,first(body),{0,0,1}));
    FK_CHECK(faceHasNonnegativeNormalProjection(body,first(body),{0,1,0}));
    const auto rational = std::make_shared<BSplineCurve<3>>(2,curve->knots(),curve->poles(),std::vector<double>{1,2,1});
    const auto rationalBody = rectangle(std::make_shared<ExtrusionSurface>(rational,Vec3(0,1,0)));
    FK_CHECK(!faceHasNonnegativeNormalProjection(rationalBody,first(rationalBody),{0,0,1}));
    const auto foldedCurve = std::make_shared<BSplineCurve<3>>(1,std::vector<double>{0,0,.5,1,1},
        std::vector<Vec3>{{0,0,0},{1,0,.5},{0,0,1}});
    const auto foldedExtrusion = rectangle(std::make_shared<ExtrusionSurface>(foldedCurve,Vec3(0,1,0)));
    FK_CHECK(faceHasNonnegativeNormalProjection(foldedExtrusion,first(foldedExtrusion),{0,0,1}));
    const auto closed = std::make_shared<BSplineCurve<3>>(1,std::vector<double>{0,0,1,2,3,3},
        std::vector<Vec3>{{0,0,0},{1,0,0},{1,0,1},{0,0,0}});
    const auto periodic = rectangle(std::make_shared<ExtrusionSurface>(closed,Vec3(0,1,0)),{.1,.4},{.2,1.2});
    FK_CHECK(periodic.face(first(periodic)).surface->isUPeriodic());
    FK_CHECK(!faceHasNonnegativeNormalProjection(periodic,first(periodic),{0,0,1}));
}

FK_TEST(FaceNormalConicPCurveAndCancellation) {
    const auto surface = folded();
    auto pcurve = std::make_shared<Circle<2>>(Vec2(.25,.5),Vec2(1,0),Vec2(0,1),.1);
    auto edge = std::make_shared<Ellipse<3>>(Vec3(.5,.5,.5),Vec3(1,0,1),Vec3(0,1,0),std::sqrt(.08),.1);
    Body::BuildFace face; face.surface = surface; face.loops = {{{0,true,pcurve}}};
    // La superficie nel primo tratto e' S(u,v)=(2u,v,2u): l'ellisse e' esatta.
    const auto body = Body::buildSheet({surface->point(.35,.5)},{{0,0,edge,{0,kTwoPi}}},{face});
    FK_CHECK(faceHasNonnegativeNormalProjection(body,first(body),{0,0,1}));
    bool cancelled = false;
    auto token = std::make_shared<std::atomic<bool>>(true);
    try { CancellationScope scope(token); faceHasNonnegativeNormalProjection(body,first(body),{0,0,1}); }
    catch (const OperationCancelled &) { cancelled = true; }
    FK_CHECK(cancelled);
}

FK_TEST(FaceNormalExtrusionTangentRationalAndEllipse) {
    const auto rational = std::make_shared<BSplineCurve<3>>(2,std::vector<double>{0,0,0,1,1,1},
        std::vector<Vec3>{{0,0,0},{.5,0,0},{1,0,1}},std::vector<double>{1,2,1});
    const auto ellipse = std::make_shared<Ellipse<3>>(Vec3(),Vec3(1,0,0),Vec3(0,0,1),2,1);
    for (const CurvePtr<3> &curve : {CurvePtr<3>(rational),CurvePtr<3>(ellipse)}) {
        auto body = rectangle(std::make_shared<ExtrusionSurface>(curve,Vec3(0,1,0)));
        FK_CHECK(faceHasNonnegativeNormalProjection(body,first(body),{0,1,0}));
        FK_CHECK(faceHasNonnegativeNormalProjection(body,first(body),{0,-1,0}));
        FK_CHECK(!faceHasNonnegativeNormalProjection(body,first(body),{0,0,1}));
        body.fin(body.fins().front()).pcurve.reset();
        FK_CHECK(faceHasNonnegativeNormalProjection(body,first(body),{0,1,0}));
    }
}
