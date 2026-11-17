#include "fk_sheet_join_validation.h"
#include "fk_bspline.h"
#include "fk_bspline_surface.h"
#include "fk_body_check.h"
#include "fk_body_io.h"
#include "fk_cancel.h"
#include "fk_unify.h"
#include "fk_test.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <vector>

using namespace ForgeCad::Kernel;
namespace {
constexpr double cap = 1e-7;
const std::vector<double> knots{0, 0, 0, 0, 1, 1, 1, 1};
struct Fixture {
    Body body;
    std::array<EdgeId, 5> logical;
    std::array<FaceId, 2> faces;
};
Fixture fixture(const Fixture *source = nullptr, bool permuted = false) {
    const std::vector<Vec3> vertices{{0, 0, 0}, {1, 1, 0}, {0, 1, 0}, {1, 0, 0}};
    std::array<SurfacePtr, 2> surfaces;
    std::array<CurvePtr<2>, 2> uv;
    CurvePtr<3> seam;
    if (source) {
        for (int i = 0; i < 2; ++i) {
            surfaces[i] = source->body.face(source->faces[i]).surface;
            for (LoopId loop : source->body.face(source->faces[i]).loops)
                for (FinId fin : source->body.loopFins(loop))
                    if (source->body.fin(fin).edge == source->logical[0]) uv[i] = source->body.fin(fin).pcurve;
        }
        seam = source->body.edge(source->logical[0]).curve;
    } else {
        for (int side = 0; side < 2; ++side) {
            std::vector<Vec3> poles;
            for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j)
                poles.emplace_back((side ? j : i) / 3.0, (side ? i : j) / 3.0, 0);
            surfaces[side] = std::make_shared<BSplineSurface>(3, 3, knots, knots, 4, 4, poles);
        }
        std::vector<Vec2> q{{0, 0}, {.2, .1}, {.8, .9}, {1, 1}};
        std::vector<Vec3> e;
        for (const auto &p : q) e.emplace_back(p.x(), p.y(), 0);
        seam = std::make_shared<BSplineCurve<3>>(3, knots, e);
        uv[0] = std::make_shared<BSplineCurve<2>>(3, knots, q);
        for (auto &p : q) std::swap(p[0], p[1]);
        uv[1] = std::make_shared<BSplineCurve<2>>(3, knots, q);
    }
    const std::array<std::array<int, 2>, 5> endpoints{{{{0, 1}}, {{1, 2}}, {{2, 0}}, {{0, 3}}, {{3, 1}}}};
    const std::array<int, 5> order = permuted ? std::array<int, 5>{{3, 2, 0, 4, 1}} : std::array<int, 5>{{0, 1, 2, 3, 4}};
    std::array<int, 5> modelIndex{};
    std::vector<Body::BuildEdge> edges;
    for (int logical : order) {
        modelIndex[logical] = int(edges.size());
        const auto start = endpoints[logical][0], end = endpoints[logical][1];
        CurvePtr<3> curve;
        if (!logical) curve = seam;
        else if (logical == 1) {
            if (source) curve = source->body.edge(source->logical[1]).curve;
            else {
                std::vector<Vec3> poles;
                for (int j = 0; j < 4; ++j) poles.push_back(vertices[start] + (vertices[end] - vertices[start]) * (j / 3.0));
                curve = std::make_shared<BSplineCurve<3>>(3, knots, poles);
            }
        } else curve = std::make_shared<Line<3>>(vertices[start], vertices[end] - vertices[start]);
        edges.push_back({start, end, curve, {0, 1}, logical == 1 ? cap / 2 : cap});
    }
    const auto lineUV = [&](int logical, bool swapped) -> CurvePtr<2> {
        const auto a = vertices[endpoints[logical][0]], b = vertices[endpoints[logical][1]];
        return std::make_shared<Line<2>>(swapped ? Vec2(a.y(), a.x()) : Vec2(a.x(), a.y()),
            swapped ? Vec2(b.y() - a.y(), b.x() - a.x()) : Vec2(b.x() - a.x(), b.y() - a.y()));
    };
    Body::BuildFace first, second;
    first.surface = surfaces[0]; second.surface = surfaces[1]; second.sense = false;
    first.loops.push_back({{modelIndex[0], true, uv[0], 0}, {modelIndex[1], true, lineUV(1, false), 0}, {modelIndex[2], true, lineUV(2, false), 0}});
    second.loops.push_back({{modelIndex[0], false, uv[1], 0}, {modelIndex[3], true, lineUV(3, true), 0}, {modelIndex[4], true, lineUV(4, true), 0}});
    Fixture result;
    result.body = Body::buildSheet(vertices, edges, permuted ? std::vector<Body::BuildFace>{second, first} : std::vector<Body::BuildFace>{first, second});
    const auto actualEdges = result.body.edges();
    const auto actualFaces = result.body.faces();
    for (int i = 0; i < 5; ++i) result.logical[order[i]] = actualEdges[i];
    result.faces = permuted ? std::array<FaceId, 2>{{actualFaces[1], actualFaces[0]}} : std::array<FaceId, 2>{{actualFaces[0], actualFaces[1]}};
    return result;
}
FinId incidence(const Fixture &f, int side = 0, int logical = 0) {
    for (LoopId loop : f.body.face(f.faces[side]).loops)
        for (FinId fin : f.body.loopFins(loop)) if (f.body.fin(fin).edge == f.logical[logical]) return fin;
    throw std::runtime_error("fixture incidence missing");
}
std::vector<detail::SheetJoinPlan> plans(const Fixture &f, bool extra = true) {
    const auto ordinal = [&](FaceId face) { const auto ids = f.body.faces(); return int(std::find(ids.begin(), ids.end(), face) - ids.begin()); };
    const auto &edge = f.body.edge(f.logical[0]);
    std::vector<detail::SheetJoinPlan> result{{ordinal(f.faces[0]), 0, cap, edge.curve, edge.range}, {ordinal(f.faces[1]), 0, cap, edge.curve, edge.range}};
    if (extra) {
        const auto &e = f.body.edge(f.logical[1]);
        result.push_back({ordinal(f.faces[0]), 1, cap / 2, e.curve, e.range});
    }
    return result;
}
std::vector<EdgeId> modelEdges(const Fixture &f) { return {f.logical[0], f.logical[1]}; }
BodyHistory explicitHistory(const Fixture &source, const Fixture &target) {
    auto history = makeHistory({&source.body}, target.body);
    for (int i = 0; i < 2; ++i)
        history.declareOrigins(EntityKind::Face, target.faces[i].index, {{0, EntityKind::Face, source.faces[i].index, HistoryRelation::Modified}});
    for (int i = 0; i < 5; ++i)
        history.declareOrigins(EntityKind::Edge, target.logical[i].index, {{0, EntityKind::Edge, source.logical[i].index, HistoryRelation::Modified}});
    return history;
}
}

FK_TEST(SheetJoinLedgerActualTwoFinsAndExtraFixedCaps) {
    auto f = fixture();
    const auto records = detail::certifySheetJoins(f.body, plans(f), modelEdges(f));
    FK_CHECK(records.size() == 3);
    FK_CHECK(f.body.fin(incidence(f, 0)).pcurveTolerance == cap);
    FK_CHECK(f.body.fin(incidence(f, 1)).pcurveTolerance == cap);
    FK_CHECK(f.body.fin(incidence(f, 0, 1)).pcurveTolerance == cap / 2);
    BodyHistory history;
    Body unified = unifySameDomain(f.body, 1e-6, &history);
    detail::requireUnifiedSheetJoins(unified, history, records);
}
FK_TEST(SheetJoinLedgerExplicitCompactionAndUnifyHistoryPermutation) {
    auto source = fixture();
    const auto records = detail::certifySheetJoins(source.body, plans(source, false), modelEdges(source));
    auto target = fixture(&source, true);
    for (int side = 0; side < 2; ++side) target.body.fin(incidence(target, side)).pcurveTolerance = cap;
    FK_CHECK(source.logical[0] != target.logical[0]);
    FK_CHECK(source.faces[0] != target.faces[0]);
    const auto history = explicitHistory(source, target);
    detail::requireUnifiedSheetJoins(target.body, history, records);
    auto candidate = fixture(nullptr, true);
    const auto mapped = detail::certifySheetJoins(candidate.body, plans(candidate, false), modelEdges(candidate));
    FK_CHECK(mapped.size() == 2 && mapped[0].edge == candidate.logical[0]);
}
FK_TEST(SheetJoinLedgerRecognizedGapCannotUseLargeFinDeviation) {
    auto f = fixture();
    auto uv = std::dynamic_pointer_cast<const BSplineCurve<2>>(f.body.fin(incidence(f)).pcurve);
    auto poles = uv->poles(); poles[1][0] += .01; poles[2][0] += .01;
    f.body.fin(incidence(f)).pcurve = std::make_shared<BSplineCurve<2>>(3, knots, poles);
    f.body.fin(incidence(f)).pcurveTolerance = 100;
    FK_CHECK_THROWS(detail::certifySheetJoins(f.body, plans(f, false), modelEdges(f)));
}
FK_TEST(SheetJoinLedgerMissingMapCurveRangeOrIncidenceRejects) {
    auto f = fixture(); auto p = plans(f, false); auto map = modelEdges(f);
    auto missing = map; missing[0] = EdgeId();
    FK_CHECK_THROWS(detail::certifySheetJoins(f.body, p, missing));
    auto badRange = p; badRange[0].range = {.125, .875};
    FK_CHECK_THROWS(detail::certifySheetJoins(f.body, badRange, map));
    auto badCurve = p; badCurve[0].curve = std::make_shared<BSplineCurve<3>>(*std::dynamic_pointer_cast<const BSplineCurve<3>>(p[0].curve));
    FK_CHECK_THROWS(detail::certifySheetJoins(f.body, badCurve, map));
    auto duplicate = p; duplicate.push_back(p.front());
    FK_CHECK_THROWS(detail::certifySheetJoins(f.body, duplicate, map));
    f.body.fin(incidence(f)).pcurve.reset();
    FK_CHECK_THROWS(detail::certifySheetJoins(f.body, p, map));
}
FK_TEST(SheetJoinLedgerInvalidBudgetAndEdgeMetadataRejects) {
    auto f = fixture(); auto p = plans(f, false);
    p[0].budget = std::numeric_limits<double>::quiet_NaN();
    FK_CHECK_THROWS(detail::certifySheetJoins(f.body, p, modelEdges(f)));
    p = plans(f, false); f.body.edge(f.logical[0]).tolerance = 100;
    FK_CHECK_THROWS(detail::certifySheetJoins(f.body, p, modelEdges(f)));
}
FK_TEST(SheetJoinLedgerUnsupportedFamilyRetainsLegacyWithoutCertificate) {
    auto f = fixture();
    for (int side = 0; side < 2; ++side) f.body.face(f.faces[side]).surface = std::make_shared<Plane>(Frame3());
    const auto records = detail::certifySheetJoins(f.body, plans(f, false), modelEdges(f));
    FK_CHECK(records.empty());
    FK_CHECK(f.body.fin(incidence(f)).pcurveTolerance == 0);
}
FK_TEST(SheetJoinLedgerUnifiedDriftAndMissingDescendantsReject) {
    auto original = fixture();
    const auto records = detail::certifySheetJoins(original.body, plans(original, false), modelEdges(original));
    const auto history = sameIndexHistory(original.body, original.body);
    for (int mode = 0; mode < 5; ++mode) {
        auto changed = original;
        auto &fin = changed.body.fin(incidence(changed));
        if (mode == 0) fin.sense = !fin.sense;
        if (mode == 1) fin.pcurve = std::make_shared<BSplineCurve<2>>(*std::dynamic_pointer_cast<const BSplineCurve<2>>(fin.pcurve));
        if (mode == 2) changed.body.edge(changed.logical[0]).range.hi = .875;
        if (mode == 3) fin.pcurveTolerance = 100;
        if (mode == 4) {
            auto shifted = fixture();
            auto support = std::dynamic_pointer_cast<const BSplineSurface>(shifted.body.face(shifted.faces[0]).surface);
            std::vector<Vec3> poles;
            for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) poles.push_back(support->pole(i,j) + Vec3(0,0,.01));
            changed.body.face(changed.faces[0]).surface = std::make_shared<BSplineSurface>(3,3,knots,knots,4,4,poles);
        }
        FK_CHECK_THROWS(detail::requireUnifiedSheetJoins(changed.body, history, records));
    }
    auto missing = makeHistory({&original.body}, original.body);
    FK_CHECK_THROWS(detail::requireUnifiedSheetJoins(original.body, missing, records));
}
FK_TEST(SheetJoinLedgerCancellationPropagates) {
    auto f = fixture(); auto token = std::make_shared<std::atomic<bool>>(true); CancellationScope scope(token);
    bool cancelled = false;
    try { (void)detail::certifySheetJoins(f.body, plans(f), modelEdges(f)); }
    catch (const OperationCancelled &) { cancelled = true; }
    FK_CHECK(cancelled);
}

FK_TEST(SheetJoinLedgerMissingCarrierOrOriginalCurveRejects) {
    auto f = fixture();
    const auto p = plans(f, false);
    f.body.face(f.faces[0]).surface.reset();
    FK_CHECK_THROWS(detail::certifySheetJoins(f.body, p, modelEdges(f)));
    f = fixture();
    const auto original = plans(f, false);
    f.body.edge(f.logical[0]).curve.reset();
    FK_CHECK_THROWS(detail::certifySheetJoins(f.body, original, modelEdges(f)));
}

FK_TEST(SheetJoinLedgerBoundedCubicCopyPreservesUnplannedSharedMap) {
    auto f=fixture();
    const std::vector<Vec2> q{{-1e-14,-1e-14},{.2,.2},{.8,.8},{1+1e-14,1+1e-14}};
    auto shared=std::make_shared<BSplineCurve<2>>(3,knots,q);
    f.body.fin(incidence(f,0)).pcurve=shared;
    f.body.fin(incidence(f,1)).pcurve=shared;
    auto &edge=f.body.edge(f.logical[0]);
    edge.curve=std::make_shared<BSplineCurve<3>>(3,knots,std::vector<Vec3>{{0,0,0},{.2,.2,0},{.8,.8,0},{1,1,0}});
    const auto curve=edge.curve; const auto range=edge.range;
    const auto carrier=f.body.face(f.faces[0]).surface;
    const auto original=writeBodyBinary(f.body);
    const double oldTolerance=f.body.fin(incidence(f)).pcurveTolerance;
    auto p=plans(f,false); p.resize(1);
    const auto emitted=detail::certifySheetJoins(f.body,p,modelEdges(f));
    FK_CHECK(emitted.size()==1);
    FK_CHECK(f.body.fin(incidence(f)).pcurve!=shared);
    FK_CHECK(f.body.fin(incidence(f,1)).pcurve==shared);
    FK_CHECK(edge.curve==curve && edge.range.lo==range.lo && edge.range.hi==range.hi && edge.tolerance==cap);
    FK_CHECK(f.body.face(f.faces[0]).surface==carrier);
    const auto actual=std::dynamic_pointer_cast<const BSplineCurve<2>>(f.body.fin(incidence(f)).pcurve);
    FK_CHECK(actual && actual->degree()==3 && actual->poles().front().x()==0 && actual->poles().front().y()==0 && actual->poles().back().x()==1 && actual->poles().back().y()==1);
    auto restored=f.body;
    restored.fin(incidence(f)).pcurve=shared;
    restored.fin(incidence(f)).pcurveTolerance=oldTolerance;
    FK_CHECK(writeBodyBinary(restored)==original);
}
FK_TEST(SheetJoinLedgerHalfUlpLineBuildsBoundedNativeMap) {
    const double hi=std::ldexp(1.,-54);
    const std::vector<double> edgeKnots{0,0,0,0,hi,hi,hi,hi};
    const Vec3 point(.5,0,0);
    const auto curve=std::make_shared<BSplineCurve<3>>(3,edgeKnots,std::vector<Vec3>(4,point));
    const auto line=std::make_shared<Line<2>>(Vec2(.5,0),Vec2(1,0));
    std::vector<Vec3> poles;
    for(int i=0;i<4;++i)for(int j=0;j<4;++j)poles.emplace_back(.5*i/3,j/3.,0);
    const std::vector<double> uKnots{0,0,0,0,.5,.5,.5,.5};
    const auto carrier=std::make_shared<BSplineSurface>(3,3,uKnots,knots,4,4,poles);
    Body body=Body::buildSheet({point},{{0,0,curve,{0,hi},cap}},{{carrier,true,{{{0,true,line,0}}}}});
    const auto edge=body.edges().front(); const auto face=body.faces().front();
    const auto fin=body.loopFins(body.face(face).loops.front()).front();
    const auto before=writeBodyBinary(body);
    FK_CHECK(line->point(hi).x()==.5); // The exact .5+2^-54 is outside even though double evaluation ties down.
    const auto emitted=detail::certifySheetJoins(body,{{0,0,cap,curve,{0,hi}}},{edge});
    FK_CHECK(emitted.size()==1);
    const auto actual=std::dynamic_pointer_cast<const BSplineCurve<2>>(body.fin(fin).pcurve);
    FK_CHECK(actual && actual->degree()==1 && actual->poles().front().x()==.5 && actual->poles().front().y()==0 && actual->poles().back().x()==.5 && actual->poles().back().y()==0);
    FK_CHECK(body.edge(edge).curve==curve && body.edge(edge).range.lo==0 && body.edge(edge).range.hi==hi);
    FK_CHECK(body.face(face).surface==carrier && body.edge(edge).tolerance==cap);
    auto restored=body; restored.fin(fin).pcurve=line; restored.fin(fin).pcurveTolerance=0;
    FK_CHECK(writeBodyBinary(restored)==before);
}
FK_TEST(SheetJoinLedgerBoundedCandidateCannotRepairPhysicalGap) {
    auto f=fixture();
    auto &fin=f.body.fin(incidence(f));
    auto uv=std::dynamic_pointer_cast<const BSplineCurve<2>>(fin.pcurve);
    auto q=uv->poles(); q.front()[0]=-.01; q.back()[0]=1.01;
    fin.pcurve=std::make_shared<BSplineCurve<2>>(3,knots,q); fin.pcurveTolerance=100;
    auto old=std::dynamic_pointer_cast<const BSplineCurve<3>>(f.body.edge(f.logical[0]).curve);
    auto poles=old->poles(); for(auto &point:poles)point[2]+=.01;
    f.body.edge(f.logical[0]).curve=std::make_shared<BSplineCurve<3>>(3,knots,poles);
    const auto curve=f.body.edge(f.logical[0]).curve;
    const auto carrier=f.body.face(f.faces[0]).surface;
    const auto before=writeBodyBinary(f.body);
    FK_CHECK_THROWS(detail::certifySheetJoins(f.body,plans(f,false),modelEdges(f)));
    FK_CHECK(f.body.edge(f.logical[0]).curve==curve && f.body.face(f.faces[0]).surface==carrier);
    FK_CHECK(f.body.edge(f.logical[0]).tolerance==cap && fin.pcurveTolerance==100);
    FK_CHECK(writeBodyBinary(f.body)==before);
}
