#include "fk_test.h"
#include "fk_blend_mitre.h"
#include "fk_bspline.h"
#include "fk_cancel.h"
#include "fk_precision.h"
#include "fk_surface_algo.h"
#include "fk_body_check.h"
#include "fk_body_io.h"
#include <algorithm>
#include <limits>
#include <memory>
using namespace ForgeCad::Kernel;

namespace {
std::shared_ptr<BSplineSurface> hiddenIso(bool bump) {
    const double a = 1.0 / 512.0, b = 3.0 / 512.0;
    const std::vector<double> y{0, a / 2, a, (a + b) / 2, b, (b + 1) / 2, 1};
    std::vector<Vec3> poles;
    for (int i = 0; i < 2; ++i)
        for (std::size_t j = 0; j < y.size(); ++j)
            poles.push_back({double(i), y[j], bump && j == 3 ? 1.0 : 0.0});
    return std::make_shared<BSplineSurface>(1, 2, std::vector<double>{0,0,1,1},
        std::vector<double>{0,0,0,a,a,b,b,1,1,1}, 2, int(y.size()), poles);
}
void checkKnownIso(double scale, bool pose, bool rational, bool swap) {
    std::vector<Vec3> poles;
    std::vector<double> weights;
    const Transform3 tr = pose ? Transform3::translation({7, -4, 2})
        * Transform3::rotation({}, Vec3(1, 2, 3), .42) * Transform3::scaling({}, scale)
        : Transform3::scaling({}, scale);
    for (int i = 0; i < 2; ++i)
        for (int j = 0; j < 3; ++j) {
            poles.push_back(tr.applyToPoint({double(i), j / 2.0, 0}));
            weights.push_back(rational ? (j == 1 ? .7 : 1.0) : 1.0);
        }
    const BSplineSurface s(1, 2, {0,0,1,1}, {0,0,0,1,1,1}, 2, 3, poles, weights);
    const BSplineSurface t(s);
    const double endpoint[4]{.3, 1, .3, 1};
    const auto cut = detail::traceBlendMitre(swap ? t : s, swap ? s : t, .3, .3, endpoint, scale, 1e-9);
    FK_CHECK(cut.curve && cut.forward);
    FK_CHECK(cut.pcurveS && cut.pcurveT);
    FK_CHECK(cut.certificateBudget == linearResolution());
    FK_CHECK(cut.range.lo == 0 && cut.range.hi == 1);
    FK_CHECK(std::isfinite(cut.gap) && cut.gap <= linearResolution());
    for (int i = 0; i <= 1024; ++i) {
        const double f = i / 1024.0;
        FK_CHECK(distance(cut.curve->point(f), s.point(.3, f)) <= linearResolution());
        FK_CHECK(distance(cut.curve->point(f), t.point(.3, f)) <= linearResolution());
    }
}
}
FK_TEST(BlendMitreIsoHiddenKnotDoesNotPublishOffSupportFit) {
    const auto s = hiddenIso(false), t = hiddenIso(true);
    const double endpoint[4]{0, 1, 0, 1};
    bool rejected = false;
    try {
        const auto cut = detail::traceBlendMitre(*s, *t, 0, 0, endpoint, 1, 1e-9);
        // The original affine UV composition may fail explicitly; a successful
        // cut must retain its declared geometric budget.
        const double at = 1.0 / 256.0;
        const Vec3 p = cut.curve->point(at);
        FK_CHECK(cut.gap <= linearResolution());
        FK_CHECK(distance(p, t->point(0, at)) <= std::max(linearResolution(), cut.gap));
    } catch (const std::domain_error &) {
        rejected = true;
    }
    (void)rejected;
}
FK_TEST(BlendMitreIsoPolynomialPositive) { checkKnownIso(1, false, false, false); }
FK_TEST(BlendMitreIsoRationalPositive) { checkKnownIso(1, false, true, false); }
FK_TEST(BlendMitreIsoRigidPosePositive) { checkKnownIso(1, true, true, false); }
FK_TEST(BlendMitreIsoScaleAndSupportOrderPositive) { checkKnownIso(2.5, true, true, true); }

namespace {
std::shared_ptr<BSplineSurface> planeAt(double z) {
    return std::make_shared<BSplineSurface>(1, 2, std::vector<double>{0,0,1,1},
        std::vector<double>{0,0,0,1,1,1}, 2, 3,
        std::vector<Vec3>{{0,0,z},{0,.5,z},{0,1,z},{1,0,z},{1,.5,z},{1,1,z}});
}
}
FK_TEST(BlendMitreDistinctFitAndCarrierBudgets) {
    const auto s = planeAt(0), t = planeAt(.5 * linearResolution());
    const double endpoint[4]{.3, 1, .3, 1};
    const auto cut = detail::traceBlendMitre(*s, *t, .3, .3, endpoint, 1, 1e-9);
    FK_CHECK(cut.curve);
    FK_CHECK(cut.gap <= linearResolution());
    for (int i = 0; i <= 128; ++i) {
        const double f = i / 128.0;
        FK_CHECK(distance(cut.curve->point(f), s->point(.3, f)) <= linearResolution());
        FK_CHECK(distance(cut.curve->point(f), t->point(.3, f)) <= linearResolution());
    }
}
FK_TEST(BlendMitreOverCarrierBudgetRejects) {
    const auto s = planeAt(0), t = planeAt(2 * linearResolution());
    const double endpoint[4]{.3, 1, .3, 1};
    bool rejected = false;
    try { (void)detail::traceBlendMitre(*s, *t, .3, .3, endpoint, 1, 1e-9); }
    catch (const std::domain_error &) { rejected = true; }
    FK_CHECK(rejected);
}
FK_TEST(BlendMitreInvalidFitToleranceRejectsBeforePublishing) {
    const auto s = planeAt(0);
    const double endpoint[4]{.3, 1, .3, 1};
    for (double value : {-1., 0., std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        bool rejected = false;
        try { (void)detail::traceBlendMitre(*s, *s, .3, .3, endpoint, 1, value); }
        catch (const std::domain_error &) { rejected = true; }
        FK_CHECK(rejected);
    }
}
FK_TEST(BlendMitreCancellationDoesNotPublishCut) {
    const auto s = planeAt(0);
    const double endpoint[4]{.3, 1, .3, 1};
    const auto previous = currentCancellation;
    bool cancelled = false, published = false;
    {
        CancellationScope scope(std::make_shared<std::atomic<bool>>(true));
        try { const auto cut = detail::traceBlendMitre(*s, *s, .3, .3, endpoint, 1, 1e-9); (void)cut; published = true; }
        catch (const OperationCancelled &) { cancelled = true; }
    }
    FK_CHECK(cancelled && !published);
    FK_CHECK(currentCancellation == previous);
}

FK_TEST(BlendMitreRevalidatesExactEmittedCoefficients) {
    const auto s = std::make_shared<BSplineSurface>(1, 2, std::vector<double>{0,0,1,1},
        std::vector<double>{0,0,0,1,1,1}, 2, 3,
        std::vector<Vec3>{{0,0,0},{0,.5,0},{0,1,0},{1,0,0},{1,.5,0},{1,1,0}});
    const double endpoint[4]{.3, 1, .3, 1};
    const auto cut = detail::traceBlendMitre(*s, *s, .3, .3, endpoint, 1, 1e-9);
    FK_CHECK(cut.certificateBudget == linearResolution());
    FK_CHECK(cut.pcurveS && cut.pcurveT);
    FK_CHECK(detail::requireBlendMitreCertificate(cut.curve, s, cut.pcurveS, cut.range, cut.certificateBudget) <= cut.certificateBudget);
    const auto *curve = dynamic_cast<const BSplineCurve<3> *>(cut.curve.get());
    FK_CHECK(curve);
    auto poles = curve->poles();
    poles[1][2] += .001;
    const auto changedCurve = std::make_shared<BSplineCurve<3>>(curve->degree(), curve->knots(), poles);
    FK_CHECK_THROWS(detail::requireBlendMitreCertificate(changedCurve, s, cut.pcurveS, cut.range, cut.certificateBudget));
    const auto changedUV = std::make_shared<BSplineCurve<2>>(1, std::vector<double>{0,0,1,1}, std::vector<Vec2>{{.4,0},{.4,1}});
    FK_CHECK_THROWS(detail::requireBlendMitreCertificate(cut.curve, s, changedUV, cut.range, cut.certificateBudget));
    FK_CHECK_THROWS(detail::requireBlendMitreCertificate(cut.curve, s, cut.pcurveS, {0,.9}, cut.certificateBudget));
}

FK_TEST(BlendMitreCoincidentDiagonalRetainsUncertifiedCompatibility) {
    const auto s = planeAt(0);
    const double endpoint[4]{1, 1, 1, 1};
    const auto cut = detail::traceBlendMitre(*s, *s, 0, 0, endpoint, 1, 1e-9);
    FK_CHECK(cut.certificateBudget == 0);
    FK_CHECK(!cut.pcurveS && !cut.pcurveT);
    for (int i = 0; i <= 128; ++i) {
        const double f = i / 128.0;
        FK_CHECK(distance(cut.curve->point(f), s->point(f, f)) <= linearResolution());
    }
}
namespace {
CurvePtr<2> mitreUV(Vec2 a, Vec2 b, Interval range = {0, 1}) {
    return std::make_shared<BSplineCurve<2>>(1,
        std::vector<double>{range.lo, range.lo, range.hi, range.hi}, std::vector<Vec2>{a, b});
}
struct EmittedMitreFixture {
    Body body;
    std::vector<detail::BlendMitreUse> plans;
};
EmittedMitreFixture emittedMitre() {
    const auto s = planeAt(0), t = planeAt(0);
    const double endpoint[4]{.3, 1, .3, 1};
    const auto cut = detail::traceBlendMitre(*s, *t, .3, .3, endpoint, 1, 1e-9);
    const double budget = cut.certificateBudget;
    const std::vector<Vec3> vertices{{.3,0,0},{.3,1,0},{0,1,0},{0,0,0},{1,0,0},{1,1,0}};
    std::vector<Body::BuildEdge> edges{{0, 1, cut.curve, cut.range, budget}};
    const std::vector<std::pair<int, int>> outer{{1,2},{2,3},{3,0},{0,4},{4,5},{5,1}};
    std::vector<CurvePtr<2>> maps;
    for (const auto &pair : outer) {
        const Vec3 a = vertices[pair.first], b = vertices[pair.second];
        const double length = distance(a, b);
        edges.push_back({pair.first, pair.second, std::make_shared<Line<3>>(a, b - a), {0, length}, 0});
        maps.push_back(mitreUV({a.x(), a.y()}, {b.x(), b.y()}, {0, length}));
    }
    Body::BuildFace a, b;
    a.surface = s;
    b.surface = t;
    a.loops = {{{0,true,{},0},{1,true,maps[0],0},{2,true,maps[1],0},{3,true,maps[2],0}}};
    b.loops = {{{0,false,{},0},{4,true,maps[3],0},{5,true,maps[4],0},{6,true,maps[5],0}}};
    std::vector<Body::BuildFace> faces{a, b};
    std::vector<detail::BlendMitreUse> plans{
        {0, cut.curve, s, cut.pcurveS, cut.range, budget},
        {0, cut.curve, t, cut.pcurveT, cut.range, budget}};
    detail::installBlendMitreCertificates(faces, plans);
    return {Body::buildSheet(vertices, edges, faces), plans};
}
}
FK_TEST(BlendMitreEmittedFinKeepsOriginalUVInBothSenses) {
    const auto fixture = emittedMitre();
    detail::requireEmittedBlendMitres(fixture.body, fixture.plans);
    FK_CHECK(checkBody(fixture.body, {true, true}).empty());
    const auto &edge = fixture.body.edge(EdgeId(0));
    FK_CHECK(edge.forward.valid() && edge.backward.valid());
    for (FinId id : {edge.forward, edge.backward}) {
        const auto &fin = fixture.body.fin(id);
        const auto &surface = fixture.body.face(fixture.body.finFace(id)).surface;
        FK_CHECK(fin.pcurve && fin.pcurveTolerance == linearResolution());
        for (double f : {0., .2, .8, 1.}) {
            const Vec2 uv = fin.pcurve->point(f);
            FK_CHECK(distance(surface->point(uv.x(), uv.y()), edge.curve->point(f)) <= linearResolution());
        }
    }
    const auto uv = mitreUV({.3, 0}, {.3, 1});
    const auto reflected = detail::originalBlendMitreUV(uv, true, {0, 1});
    for (double f : {0., .25, 1.}) {
        FK_CHECK_NEAR(reflected->point(f).x(), .3, 0);
        FK_CHECK_NEAR(reflected->point(f).y(), 1 - f, 0);
    }
}
FK_TEST(BlendMitreEmissionRejectsChangedProvenanceAndBudgets) {
    for (int mutation = 0; mutation < 7; ++mutation) {
        auto fixture = emittedMitre();
        Body &body = fixture.body;
        const EdgeId edge(0);
        const FinId fin = body.edge(edge).forward;
        switch (mutation) {
        case 0: body.fin(fin).pcurve = mitreUV({.4,0}, {.4,1}); break;
        case 1: body.fin(fin).pcurveTolerance *= 2; break;
        case 2: body.edge(edge).tolerance *= 2; break;
        case 3: body.edge(edge).range.hi = .9; break;
        case 4: body.face(body.finFace(fin)).surface = planeAt(.001); break;
        case 5: body.vertex(body.edgeStart(edge)).point[2] += .001; break;
        case 6: body.fin(fin).pcurve.reset(); break;
        }
        FK_CHECK_THROWS(detail::requireEmittedBlendMitres(body, fixture.plans));
    }
}
