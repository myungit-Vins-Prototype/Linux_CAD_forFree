#include "fk_test.h"
#include "fk_surface_algo.h"
#include "fk_bspline_surface.h"
#include "fk_body_io.h"
#include "fk_curve_ops.h"

#include <limits>

using namespace ForgeCad::Kernel;
namespace {
BSplineSurface patch(std::vector<double> weights = {}) {
    return BSplineSurface(1,1,{0,0,1,1},{0,0,1,1},2,2,{{0,0,0},{0,1,.2},{1,0,.4},{1,1,.8}},std::move(weights));
}
SurfacePtr codec(const SurfacePtr &surface) {
    Body::BuildFace face; face.surface=surface;
    const Body source=Body::buildSheet({}, {}, {face});
    const auto restored=readBodyBinary(writeBodyBinary(source));
    return restored.face(restored.faces().front()).surface;
}
CurvePtr<3> spline(std::vector<double> weights = {}) {
    return std::make_shared<BSplineCurve<3>>(2,std::vector<double>{0,0,0,1,1,1},
        std::vector<Vec3>{{0,0,0},{1,1,.5},{2,0,2}},std::move(weights));
}
class UnknownCurve final : public Curve<3> {
public:
    CurveType type() const override { return CurveType::Other; }
    Interval domain() const override { return {0,1}; }
    void evaluate(double t,int order,Vec3*out) const override {out[0]={t,0,t};for(int i=1;i<=order;++i)out[i]=i==1?Vec3(1,0,1):Vec3();}
};
}

FK_TEST(SurfaceParameterizationBSplineCopiesAndCodec) {
    for (const auto &weights : {std::vector<double>{},std::vector<double>{1,2,3,4}}) {
        const auto a=patch(weights); const auto b=a;
        FK_CHECK(&a!=&b && sameSurfaceParameterization(a,b));
        FK_CHECK(sameSurface(a,b,0));
        const auto source=std::make_shared<BSplineSurface>(a); const auto restored=codec(source);
        FK_CHECK(source.get()!=restored.get() && sameSurfaceParameterization(*source,*restored));
        FK_CHECK(sameSurface(*source,*restored,0));
    }
    const auto a=patch(), rationalOnes=patch({1,1,1,1});
    FK_CHECK(!sameSurfaceParameterization(a,rationalOnes));
    const auto changed=BSplineSurface(1,1,{0,0,1,1},{0,0,1,1},2,2,
        {{0,0,0},{0,1,.2},{1,0,.4},{1,1,std::nextafter(.8,1.)}});
    FK_CHECK(!sameSurfaceParameterization(a,changed));
    FK_CHECK(!sameSurface(a,changed,1e-3)); // nessun confronto per campioni o tolleranza
    const auto movedDomain=BSplineSurface(1,1,{2,2,3,3},{0,0,1,1},2,2,
        {{0,0,0},{0,1,.2},{1,0,.4},{1,1,.8}});
    FK_CHECK(!sameSurfaceParameterization(a,movedDomain));
    const auto reversed=BSplineSurface(1,1,{0,0,1,1},{0,0,1,1},2,2,
        {{1,0,.4},{1,1,.8},{0,0,0},{0,1,.2}});
    FK_CHECK(!sameSurfaceParameterization(a,reversed));
    const auto swapped=BSplineSurface(1,1,{0,0,1,1},{0,0,1,1},2,2,
        {{0,0,0},{1,0,.4},{0,1,.2},{1,1,.8}});
    FK_CHECK(!sameSurfaceParameterization(a,swapped));
    FK_CHECK(!sameSurfaceParameterization(patch({1,2,3,4}),patch({1,2,3,5})));
}

FK_TEST(SurfaceParameterizationExtrusionCurveFamilies) {
    const std::vector<CurvePtr<3>> curves{
        std::make_shared<Line<3>>(Vec3(1,2,3),Vec3(1,0,0)),
        std::make_shared<Circle<3>>(Vec3(1,2,3),Vec3(1,0,0),Vec3(0,0,1),2),
        std::make_shared<Ellipse<3>>(Vec3(1,2,3),Vec3(1,0,0),Vec3(0,0,1),2,1),
        spline(), spline({1,2,3}),
        std::make_shared<TrimmedCurve<3>>(spline(),.2,.8),
        std::make_shared<TransformedCurve>(spline(),Transform3::translation({1,2,3}))
    };
    for (const auto &curve : curves) {
        const auto source=std::make_shared<ExtrusionSurface>(curve,Vec3(0,1,0));
        const auto one=codec(source), two=codec(source);
        FK_CHECK(one.get()!=two.get() && sameSurfaceParameterization(*one,*two));
        FK_CHECK(sameSurfaceParameterization(*source,*one));
        const auto reversed=ExtrusionSurface(reversedCurve(curve),Vec3(0,1,0));
        FK_CHECK(!sameSurfaceParameterization(*source,reversed));
        const auto directionChanged=ExtrusionSurface(curve,Vec3(0,-1,0));
        FK_CHECK(!sameSurfaceParameterization(*source,directionChanged));
    }
    const auto base=spline();
    const auto a=ExtrusionSurface(base,{0,1,0});
    const auto shifted=ExtrusionSurface(translatedCurve<3>(base,{0,1,0}),{0,1,0});
    FK_CHECK(sameSurface(a,shifted,1e-9));
    FK_CHECK(!sameSurfaceParameterization(a,shifted)); // stessa superficie, V traslato
    const auto subdivided=ExtrusionSurface(std::make_shared<BSplineCurve<3>>(static_cast<const BSplineCurve<3>&>(*base).insertKnot(.5)),{0,1,0});
    FK_CHECK(!sameSurfaceParameterization(a,subdivided)); // rappresentazione diversa: prudente
    const auto trimA=ExtrusionSurface(std::make_shared<TrimmedCurve<3>>(base,.1,.8),{0,1,0});
    const auto trimB=ExtrusionSurface(std::make_shared<TrimmedCurve<3>>(base,.2,.8),{0,1,0});
    FK_CHECK(!sameSurfaceParameterization(trimA,trimB));
}

FK_TEST(SurfaceParameterizationAnalyticOriginsAndAxes) {
    const Frame3 original;
    const Frame3 translated({1,0,0},{0,0,1},{1,0,0});
    const Frame3 rotated({0,0,0},{0,0,1},{0,1,0});
    const auto a=Plane(original), b=Plane(translated), c=Plane(rotated);
    FK_CHECK(sameSurface(a,b,0) && sameSurface(a,c,0));
    FK_CHECK(!sameSurfaceParameterization(a,b) && !sameSurfaceParameterization(a,c));
    FK_CHECK(sameSurfaceParameterization(a,Plane(original)));
    FK_CHECK(sameSurfaceParameterization(CylindricalSurface(original,2),CylindricalSurface(original,2)));
    FK_CHECK(!sameSurfaceParameterization(CylindricalSurface(original,2),CylindricalSurface(rotated,2)));
    FK_CHECK(sameSurfaceParameterization(ConicalSurface(original,.3,2),ConicalSurface(original,.3,2)));
    FK_CHECK(sameSurfaceParameterization(SphericalSurface(original,2),SphericalSurface(original,2)));
    FK_CHECK(sameSurfaceParameterization(ToroidalSurface(original,3,1),ToroidalSurface(original,3,1)));
    const auto meridian=spline();
    FK_CHECK(sameSurfaceParameterization(RevolutionSurface(meridian,{},Vec3(0,0,1)),RevolutionSurface(meridian,{},Vec3(0,0,1))));
    FK_CHECK(!sameSurfaceParameterization(RevolutionSurface(meridian,{},Vec3(0,0,1)),RevolutionSurface(meridian,{},Vec3(0,0,-1))));
}

FK_TEST(SurfaceParameterizationUnknownAndCancellation) {
    const auto one=std::make_shared<UnknownCurve>(), two=std::make_shared<UnknownCurve>();
    const auto a=ExtrusionSurface(one,{0,1,0}), b=ExtrusionSurface(two,{0,1,0}), same=ExtrusionSurface(one,{0,1,0});
    FK_CHECK(!sameSurfaceParameterization(a,b));
    FK_CHECK(sameSurfaceParameterization(a,same));
    bool cancelled=false;
    try {CancellationScope scope(std::make_shared<std::atomic<bool>>(true));sameSurfaceParameterization(a,b);}
    catch(const OperationCancelled &){cancelled=true;}
    FK_CHECK(cancelled);
}
