#include "fk_surface_curve_bounds.h"
#include "fk_bspline.h"
#include "fk_bspline_surface.h"
#include "fk_cancel.h"
#include "fk_test.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <vector>

using namespace ForgeCad::Kernel;
namespace {
using detail::SurfaceCurveBoundStatus;
struct WeightedAxis {
    std::vector<double> knots, poles, weights;
};
WeightedAxis axis(int degree, const std::vector<double> &breaks, const std::vector<double> &weights) {
    WeightedAxis result;
    result.knots.assign(degree + 1, breaks.front());
    for (size_t k = 1; k + 1 < breaks.size(); ++k) result.knots.insert(result.knots.end(), degree, breaks[k]);
    result.knots.insert(result.knots.end(), degree + 1, breaks.back());
    for (size_t k = 1; k < breaks.size(); ++k) {
        const double a = breaks[k - 1], d = breaks[k] - a, w = weights[k - 1], dw = weights[k] - w;
        for (int i = k == 1 ? 0 : 1; i <= degree; ++i) {
            const double denominator = w + dw * i / degree;
            const double numerator = a * w + (a * dw + d * w) * i / degree
                + d * dw * i * (i - 1) / (degree * (degree - 1));
            result.weights.push_back(denominator);
            result.poles.push_back(numerator / denominator);
        }
    }
    return result;
}
std::shared_ptr<BSplineSurface> identityCarrier(int pu, int pv,
    const std::vector<double> &ub = {0, 1}, const std::vector<double> &uw = {1, 3},
    const std::vector<double> &vb = {0, 1}, const std::vector<double> &vw = {1, 2},
    double weightScale = 1, double scale = 1, Vec3 shift = Vec3()) {
    // Homogeneous Hx=u*W, Hy=v*W, W=Wu(u)*Wv(v). The resulting
    // original IEEE controls are the fixture; no rounded extraction is used.
    const auto u = axis(pu, ub, uw), v = axis(pv, vb, vw);
    std::vector<Vec3> poles; std::vector<double> weights;
    for (size_t i = 0; i < u.poles.size(); ++i) for (size_t j = 0; j < v.poles.size(); ++j) {
        poles.push_back(shift + scale * Vec3(u.poles[i], v.poles[j], 0));
        weights.push_back(weightScale * u.weights[i] * v.weights[j]);
    }
    return std::make_shared<BSplineSurface>(pu, pv, u.knots, v.knots, int(u.poles.size()), int(v.poles.size()), poles, weights);
}
std::shared_ptr<BSplineCurve<2>> mapUV(Interval domain = {0, 1}) {
    return std::make_shared<BSplineCurve<2>>(3,
        std::vector<double>{domain.lo, domain.lo, domain.lo, domain.lo, domain.hi, domain.hi, domain.hi, domain.hi},
        std::vector<Vec2>{{0, .1}, {.2, .8}, {.8, .2}, {1, .9}});
}
std::shared_ptr<BSplineCurve<3>> mappedEdge(const BSplineCurve<2> &uv, double scale = 1, Vec3 shift = Vec3()) {
    std::vector<Vec3> poles;
    for (const auto &q : uv.poles()) poles.push_back(shift + scale * Vec3(q.x(), q.y(), 0));
    return std::make_shared<BSplineCurve<3>>(3, uv.knots(), poles);
}
void expect(const CurvePtr<3> &e, const SurfacePtr &s, const CurvePtr<2> &q,
            SurfaceCurveBoundStatus status, Interval range = {0, 1}, double budget = 1e-8) {
    const auto result = detail::surfaceCurveResidualBound(e, s, q, range, budget);
    FK_CHECK(result.status == status);
    if (status == SurfaceCurveBoundStatus::Certified)
        FK_CHECK(std::isfinite(result.upperBound) && result.upperBound >= 0 && result.upperBound <= budget);
}
}

FK_TEST(RationalCarrierNativeQuadraticCubicAndMixedIdentity) {
    auto uv = mapUV(); auto edge = mappedEdge(*uv);
    for (int pu : {2, 3}) for (int pv : {2, 3})
        expect(edge, identityCarrier(pu, pv), uv, SurfaceCurveBoundStatus::Certified);
}
FK_TEST(RationalCarrierCellExtrapolatedDenominatorMayBeNegative) {
    auto uv = std::make_shared<BSplineCurve<2>>(1, std::vector<double>{0, 0, 1, 1}, std::vector<Vec2>{{0, .25}, {1, .75}});
    auto edge = std::make_shared<BSplineCurve<3>>(3, std::vector<double>{0, 0, 0, 0, 1, 1, 1, 1},
        std::vector<Vec3>{{0, .25, 0}, {1.0/3, 5.0/12, 0}, {2.0/3, 7.0/12, 0}, {1, .75, 0}});
    // First-cell W=10-18u and last-cell W=18u-8 extrapolate to -8
    // across the full range. On their own closed cells all weights >=1.
    FK_CHECK(10 + (1 - 10) * 2 < 0);
    for (int degree : {2, 3})
        expect(edge, identityCarrier(degree, degree, {0, .5, 1}, {10, 1, 10}, {0, 1}, {1, 1}), uv,
            SurfaceCurveBoundStatus::Certified);
}
FK_TEST(RationalCarrierWeightGaugeAndPhysicalScale) {
    for (double weights : {std::ldexp(1., -200), 1., std::ldexp(1., 200)})
        for (double scale : {.01, 1000.}) {
            const Vec3 shift(2 * scale, -3 * scale, 5 * scale);
            auto uv = mapUV();
            expect(mappedEdge(*uv, scale, shift), identityCarrier(2, 3, {0, 1}, {1, 3}, {0, 1}, {1, 2}, weights, scale, shift),
                uv, SurfaceCurveBoundStatus::Certified, {0, 1}, 1e-8 * std::max(1., scale));
        }
}
FK_TEST(RationalCarrierNonUnitDomainTrimAndCorrelatedReverse) {
    auto support = identityCarrier(2, 3, {-3, 5}, {1, 3}, {7, 11}, {1, 2});
    auto uv = std::make_shared<BSplineCurve<2>>(3, std::vector<double>{-2,-2,-2,-2,3,3,3,3},
        std::vector<Vec2>{{-2,7.5},{-1,8},{2,9},{4,10.5}});
    expect(mappedEdge(*uv), support, uv, SurfaceCurveBoundStatus::Certified, {-1.75,2.125});
    auto poles = uv->poles(); std::reverse(poles.begin(), poles.end());
    auto reversed = std::make_shared<BSplineCurve<2>>(3, uv->knots(), poles);
    expect(mappedEdge(*reversed), support, reversed, SurfaceCurveBoundStatus::Certified, {-1.75,2.125});
    expect(mappedEdge(*uv), support, reversed, SurfaceCurveBoundStatus::Failed, {-1.75,2.125});
}
FK_TEST(RationalCarrierChangedWeightIsRecognizedGap) {
    const std::vector<double> knots{0,0,0,0,1,1,1,1};
    std::vector<Vec3> poles; for (int i=0;i<4;++i) for (int j=0;j<4;++j) poles.emplace_back(i/3.,j/3.,0);
    auto uv = mapUV(); auto edge = mappedEdge(*uv);
    expect(edge, std::make_shared<BSplineSurface>(3,3,knots,knots,4,4,poles), uv, SurfaceCurveBoundStatus::Certified);
    std::vector<double> weights(16,1); weights[5]=.25;
    expect(edge, std::make_shared<BSplineSurface>(3,3,knots,knots,4,4,poles,weights), uv, SurfaceCurveBoundStatus::Failed);
}
FK_TEST(RationalCarrierUVOutsideOriginalDomainFails) {
    auto uv = std::make_shared<BSplineCurve<2>>(3, std::vector<double>{0,0,0,0,1,1,1,1},
        std::vector<Vec2>{{0,0},{13./3,1./3},{-10./3,2./3},{1,1}});
    expect(mappedEdge(*uv), identityCarrier(2,3), uv, SurfaceCurveBoundStatus::Failed);
}
FK_TEST(RationalCarrierCancellationPropagates) {
    auto uv = mapUV(); auto edge = mappedEdge(*uv); auto surface = identityCarrier(2,3);
    auto token = std::make_shared<std::atomic<bool>>(true); CancellationScope scope(token);
    bool cancelled = false;
    try { (void)detail::surfaceCurveResidualBound(edge,surface,uv,{0,1},1e-8); }
    catch (const OperationCancelled &) { cancelled = true; }
    FK_CHECK(cancelled);
}

FK_TEST(RationalCarrierHiddenKnotCellPeakFailsContinuously) {
    const std::vector<double> breaks{0, .201, .202, .203, 1};
    auto source = identityCarrier(3, 3, breaks, {1, 1.25, 1.5, 1.25, 1});
    auto uv = std::make_shared<BSplineCurve<2>>(1, std::vector<double>{0,0,1,1}, std::vector<Vec2>{{0,0},{1,1}});
    auto edge = std::make_shared<BSplineCurve<3>>(3, std::vector<double>{0,0,0,0,1,1,1,1},
        std::vector<Vec3>{{0,0,0},{1./3,1./3,0},{2./3,2./3,0},{1,1,0}});
    expect(edge, source, uv, SurfaceCurveBoundStatus::Certified);
    std::vector<Vec3> poles; std::vector<double> weights;
    for (int i=0;i<source->uPoleCount();++i) for (int j=0;j<source->vPoleCount();++j) {
        auto point=source->pole(i,j); if (i==6) point[2]=1;
        poles.push_back(point); weights.push_back(source->weight(i,j));
    }
    auto peak=std::make_shared<BSplineSurface>(3,3,source->uKnots(),source->vKnots(),source->uPoleCount(),source->vPoleCount(),poles,weights);
    // These samples only demonstrate aliasing. The provider must reject the
    // whole range; the node witness is exported separately for exact audit.
    for (double t : {0.,.25,.5,.75,1.}) {
        const auto q=uv->point(t);
        FK_CHECK(norm(edge->point(t)-peak->point(q.x(),q.y()))<1e-12);
    }
    expect(edge, peak, uv, SurfaceCurveBoundStatus::Failed);
}
FK_TEST(RationalCarrierFixedOriginalScalarLimitFailsClosed) {
    std::vector<double> breaks, weights;
    for (int i=0;i<=41667;++i) {
        breaks.push_back(double(i)/41667);
        weights.push_back(1+double(i)/41667);
    }
    auto surface=identityCarrier(3,3,breaks,weights);
    FK_CHECK(size_t(surface->uPoleCount())*surface->vPoleCount()>500000);
    auto uv=mapUV();
    expect(mappedEdge(*uv),surface,uv,SurfaceCurveBoundStatus::Failed);
}
