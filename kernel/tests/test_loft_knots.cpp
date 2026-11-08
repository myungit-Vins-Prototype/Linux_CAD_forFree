#include "fk_test.h"
#include "fk_loft_knots.h"
#include "fk_bspline_surface.h"
#include "fk_body_check.h"
#include "fk_body_io.h"
#include "fk_nurbs.h"
#include "fk_offset.h"
#include "fk_sew.h"
#include "fk_surface_algo.h"
#include "fk_sheet.h"
#include <algorithm>
#include <cmath>
#include <limits>
using namespace ForgeCad::Kernel;
namespace {
BSplineCurve<3> baseCurve(bool rational) {
    std::vector<Vec3> poles; std::vector<double> weights;
    for (int j = 0; j <= 5; ++j) {
        poles.push_back({2.*j,0,.2*j*(j-1)});
        if (rational) weights.push_back(1.+.03*j);
    }
    return {5,{0,0,0,0,0,0,1,1,1,1,1,1},poles,weights};
}
std::shared_ptr<BSplineSurface> strip(const BSplineCurve<3> &curve, bool swap = false) {
    std::vector<Vec3> poles; std::vector<double> weights;
    if (!swap) for (double y : {0.,10.}) for (int j = 0; j < curve.poleCount(); ++j) {
        poles.push_back(curve.poles()[j]+Vec3{0,y,0}); weights.push_back(curve.weight(j));
    }
    else for (int j = 0; j < curve.poleCount(); ++j) for (double y : {0.,10.}) {
        poles.push_back(curve.poles()[j]+Vec3{0,y,0}); weights.push_back(curve.weight(j));
    }
    return std::make_shared<BSplineSurface>(swap?curve.degree():1,swap?1:curve.degree(),
        swap?curve.knots():std::vector<double>{0,0,1,1},swap?std::vector<double>{0,0,1,1}:curve.knots(),
        swap?curve.poleCount():2,swap?2:curve.poleCount(),poles,curve.isRational()?weights:std::vector<double>{});
}
Body sheet(const SurfacePtr &s) {
    const auto u = s->uDomain(), v = s->vDomain();
    return Body::buildSheet({s->point(u.lo,v.lo),s->point(u.hi,v.lo),s->point(u.hi,v.hi),s->point(u.lo,v.hi)},
        {{0,1,s->vIso(v.lo),u},{1,2,s->uIso(u.hi),v},{3,2,s->vIso(v.hi),u},{0,3,s->uIso(u.lo),v}},
        {{s,true,{{{0,true,{}},{1,true,{}},{2,false,{}},{3,false,{}}}}}});
}
}

FK_TEST(SharpKnotLinesRespectSplineContinuityAtAdjacentDoubles) {
    const double knot = .35409618429145934, next = std::nextafter(knot,1.);
    for (bool rational : {false,true}) for (bool swap : {false,true}) {
        const auto source = baseCurve(rational);
        const auto refined = source.insertKnot(knot,3).insertKnot(next,3);
        const auto surface = strip(refined,swap);
        const auto sharp = surface->cachedSharpKnotLines();
        FK_CHECK(sharp->u.empty() && sharp->v.empty());
        for (double t : {.1,.3,knot,.5,.9}) FK_CHECK(distance(source.point(t),refined.point(t)) < 1e-13);
    }
    // Nodo C0 con piega reale: deve continuare a essere rilevato.
    const BSplineCurve<3> crease(1,{0,0,.5,1,1},{{0,0,0},{5,0,0},{5,0,5}});
    const auto sharp = strip(crease)->cachedSharpKnotLines();
    FK_CHECK(sharp->v.size() == 1 && sharp->v.front() == .5);
}

FK_TEST(LoftGuideKnotUnionPreservesCurveWithinRounding) {
    const double anchor = .61230909488562113;
    const double shifted = std::nextafter(std::nextafter(anchor,1.),1.);
    for (bool rational : {false,true}) {
        const auto source = baseCurve(rational);
        std::vector<double> anchors(6,0.); anchors.insert(anchors.end(),5,anchor); anchors.insert(anchors.end(),6,1.);
        std::vector<BSplineCurvePtr<3>> curves{
            std::make_shared<BSplineCurve<3>>(source.insertKnot(anchor,5)),
            std::make_shared<BSplineCurve<3>>(source.insertKnot(shifted,5)),nullptr};
        const auto before = curves;
        const auto knots = detail::harmonizeLoftGuideKnots(curves,anchors);
        FK_CHECK(std::count(knots.begin(),knots.end(),anchor) == 5);
        FK_CHECK(std::find(knots.begin(),knots.end(),shifted) == knots.end());
        for (int i = 0; i < 2; ++i) for (int j = 0; j <= 100; ++j)
            FK_CHECK(distance(curves[i]->point(j/100.),before[i]->point(j/100.)) < 2e-12);
        FK_CHECK(!curves.back());
    }
}

FK_TEST(LoftGuideKnotUnionPreservesDistinctDetailsAndVetoesLargeChanges) {
    const double a = .5, b = std::nextafter(a,1.);
    const auto source = baseCurve(false).insertKnot(a).insertKnot(b);
    std::vector<BSplineCurvePtr<3>> curves{std::make_shared<BSplineCurve<3>>(source)};
    const auto knots = detail::harmonizeLoftGuideKnots(curves,{0,0,0,0,0,0,1,1,1,1,1,1});
    FK_CHECK(std::find(knots.begin(),knots.end(),a) != knots.end());
    FK_CHECK(std::find(knots.begin(),knots.end(),b) != knots.end());
    // Una campata microscopica puo' rappresentare un dettaglio macroscopico:
    // non si accetta il cambio di nodi soltanto per la distanza parametrica.
    const double c = a+8*std::numeric_limits<double>::epsilon(), target = a+4*std::numeric_limits<double>::epsilon();
    const auto detailCurve = std::make_shared<BSplineCurve<3>>(1,std::vector<double>{0,0,a,c,1,1},
        std::vector<Vec3>{{0,0,0},{0,0,1},{1,0,1},{1,0,2}});
    curves = {detailCurve};
    FK_CHECK_THROWS(detail::harmonizeLoftGuideKnots(curves,{0,0,target,1,1}));
    FK_CHECK(curves.front() == detailCurve);
}

FK_TEST(OffsetOfRefinedSmoothStripIsInvariant) {
    const auto source = baseCurve(false);
    const double knot = .35409618429145934;
    const auto refined = source.insertKnot(knot,3).insertKnot(std::nextafter(knot,1.),3);
    const auto original = sheet(strip(source)), input = sheet(strip(refined));
    const auto frozen = writeBodyBinary(input);
    for (double distanceValue : {-3.,3.}) {
        const auto a = offsetFaces(original,original.faces(),distanceValue,1e-7,true);
        const auto b = offsetFaces(input,input.faces(),distanceValue,1e-7,true);
        FK_CHECK(b.body.faces().size() == a.body.faces().size());
        FK_CHECK(checkBody(b.body,{true,true}).empty());
        for (double u : {.2,.5,.8}) for (double v : {.1,.3,.6,.9}) {
            const auto &parent = *original.face(original.faces().front()).surface;
            const auto expected = parent.point(u,v)+distanceValue*parent.normal(u,v);
            FK_CHECK(distance(b.body.face(b.body.faces().front()).surface->point(u,v),expected) < 1e-7);
        }
    }
    FK_CHECK(writeBodyBinary(input) == frozen);
}

FK_TEST(OffsetPreservesSeamsThroughNearlyTangentSections) {
    const auto make = [](bool right) {
        std::vector<Vec3> poles;
        for (int u = 0; u < 2; ++u) for (int v = 0; v < 3; ++v)
            poles.push_back({right?10.*u:-10.+10.*u,5.*v,right&&u&&v==1?.1:0.});
        return sheet(std::make_shared<BSplineSurface>(1,2,std::vector<double>{0,0,1,1},
            std::vector<double>{0,0,0,1,1,1},2,3,poles));
    };
    const auto left = make(false), right = make(true);
    const auto input = sewSheets({&left,&right},1e-7,false).body;
    FK_CHECK(input.faces().size() == 2 && input.edges().size() == 7);
    for (double d : {-3.,3.}) {
        const auto result = offsetFaces(input,input.faces(),d,1e-7,true);
        FK_CHECK(result.shells == 1 && result.body.faces().size() == 2);
        FK_CHECK(checkBody(result.body,{true,true}).empty());
        int joined = 0;
        for (auto e : result.body.edges()) if (!result.body.isLaminar(e)) {
            ++joined; const auto &edge = result.body.edge(e);
            for (double fraction : {.1,.3,.5,.7,.9}) {
                const auto point = edge.curve->point(edge.range.lo+fraction*edge.range.length());
                for (auto f : input.faces()) {
                    // Il trim puo' prolungare i supporti oltre il vecchio
                    // bordo. La distanza va misurata sul supporto esteso.
                    const auto extended = extendBSplineSurface(static_cast<const BSplineSurface&>(*input.face(f).surface),{-.1,1.1},{0,1});
                    FK_CHECK_NEAR(projectPoint(extended,point).distance,std::abs(d),1e-7);
                }
            }
        }
        FK_CHECK(joined == 1);
    }
}
