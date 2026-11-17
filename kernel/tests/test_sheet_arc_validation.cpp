#include "fk_sheet.h"
#include "fk_body_check.h"
#include "fk_body_io.h"
#include "fk_cancel.h"
#include "fk_classify.h"
#include "fk_curve_algo.h"
#include "fk_extrude.h"
#include "fk_history.h"
#include "fk_test.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>

using namespace ForgeCad::Kernel;
namespace {
struct Curtain {
    Body body;
    CurvePtr<3> base;
    EdgeId end, start;
    double scale;
};
Curtain curtain(double epsilon = 0, double scale = 1, const Frame3 &frame = Frame3(), bool tensor = false) {
    // Cx=(u-2)^3 on [-1,1], Cy=epsilon*u: a native regular source
    // extrusion. The zero-speed point u=2 is outside the source domain.
    const std::vector<double> knots{-1, -1, -1, -1, 1, 1, 1, 1};
    const double xs[]{-27, -9, -3, -1};
    std::vector<Vec3> poles;
    for (int i = 0; i != 4; ++i)
        poles.push_back(frame.toGlobal(scale * Vec3(xs[i], epsilon * (-1 + 2 * i / 3.), 0)));
    const auto base = std::make_shared<BSplineCurve<3>>(3, knots, poles);
    auto upperPoles = poles;
    const Vec3 offset = scale * frame.zDir();
    for (Vec3 &p : upperPoles) p += offset;
    const auto upper = std::make_shared<BSplineCurve<3>>(3, knots, upperPoles);
    const Vec3 a = base->point(-1), b = base->point(1);
    std::vector<Body::BuildEdge> edges{
        {0, 1, base, {-1, 1}, 0},
        {1, 2, std::make_shared<Line<3>>(b, frame.zDir()), {0, scale}, 0},
        {3, 2, upper, {-1, 1}, 0},
        {0, 3, std::make_shared<Line<3>>(a, frame.zDir()), {0, scale}, 0}
    };
    auto uv = [](Vec2 origin, Vec2 direction) { return std::make_shared<Line<2>>(origin, direction); };
    Body::BuildFace face;
    if (tensor) {
        // Native tensor product of exactly the same base and linear V.
        // Ordering follows BSplineSurface: V index varies fastest.
        std::vector<Vec3> tensorPoles;
        for (std::size_t i = 0; i < poles.size(); ++i) {
            tensorPoles.push_back(poles[i]);
            tensorPoles.push_back(upperPoles[i]);
        }
        face.surface = std::make_shared<BSplineSurface>(3, 1, knots,
            std::vector<double>{0, 0, scale, scale}, 4, 2, tensorPoles);
    } else {
        face.surface = std::make_shared<ExtrusionSurface>(base, frame.zDir());
    }
    face.loops.push_back({{0, true, uv({0, 0}, {1, 0}), 0},
                          {1, true, uv({1, 0}, {0, 1}), 0},
                          {2, false, uv({0, scale}, {1, 0}), 0},
                          {3, false, uv({-1, 0}, {0, 1}), 0}});
    Body body = Body::buildSheet({a, b, b + offset, a + offset}, edges, {face});
    // BuildEdge order is preserved; these are the two declared vertical
    // incidences, with no geometric nearest-edge selection.
    const auto ids = body.edges();
    return {std::move(body), base, ids.at(1), ids.at(3), scale};
}
void emptyHistory(const BodyHistory &history) {
    FK_CHECK(history.operandCount() == 0);
    for (EntityKind kind : {EntityKind::Face, EntityKind::Edge, EntityKind::Vertex})
        FK_CHECK(history.resultSize(kind) == 0);
}
double operationBudget(const Body &source) {
    // Same pre-existing physical budget as extendSheet, fixed from INPUT.
    Box box;
    for (VertexId v : source.vertices()) box.add(source.vertex(v).point);
    for (FaceId f : source.faces()) box.add(faceBox(source, f));
    return 1e-9 * std::max(box.diagonal(), 1.);
}
void rejectsWrongReach(double epsilon, bool tensor) {
    const auto fixture = curtain(epsilon, 1, Frame3(), tensor);
    FK_CHECK(checkBody(fixture.body).empty());
    const auto before = writeBodyBinary(fixture.body);
    BodyHistory history;
    bool rejected = false;
    try { (void)extendSheet(fixture.body, {fixture.end}, 3, false, &history); }
    catch (const std::domain_error &) { rejected = true; }
    FK_CHECK(rejected);
    emptyHistory(history);
    FK_CHECK(writeBodyBinary(fixture.body) == before);
}
void naturalReach(const Curtain &fixture, bool atEnd, double requested) {
    FK_CHECK(checkBody(fixture.body).empty());
    const auto before = writeBodyBinary(fixture.body);
    const double budget = operationBudget(fixture.body);
    BodyHistory history;
    const Body result = extendSheet(fixture.body, {atEnd ? fixture.end : fixture.start}, requested, false, &history);
    FK_CHECK(result.isSheet());
    FK_CHECK(checkBody(result).empty());
    const auto descendants = history.descendants(EntityKind::Face, 0, fixture.body.faces().front().index);
    FK_CHECK(descendants.size() == 1);
    if (descendants.size() != 1) return;
    const FaceId face(descendants.front());
    const auto &surface = result.face(face).surface;
    FK_CHECK(surface->type() == SurfaceType::Extrusion || surface->type() == SurfaceType::BSpline);
    if (surface->type() != SurfaceType::Extrusion && surface->type() != SurfaceType::BSpline) return;
    const CurvePtr<3> actual = surface->vIso(.5 * fixture.scale);
    FK_CHECK(bool(actual));
    if (!actual) return;
    // Read the boundary parameter from the actually emitted fin incidences,
    // rather than assuming carrier-domain extension is used by the face.
    double end = atEnd ? -std::numeric_limits<double>::infinity() : std::numeric_limits<double>::infinity();
    int incidences = 0;
    for (FinId f : result.fins()) {
        if (result.finFace(f) != face) continue;
        const Fin &fin = result.fin(f);
        FK_CHECK(bool(fin.pcurve));
        if (!fin.pcurve) continue;
        const auto range = result.edge(fin.edge).range;
        for (double t : {range.lo, range.hi}) {
            const double u = fin.pcurve->point(t).x();
            end = atEnd ? std::max(end, u) : std::min(end, u);
        }
        ++incidences;
    }
    FK_CHECK(incidences >= 4);
    FK_CHECK(std::isfinite(end));
    const double start = atEnd ? 1 : fixture.base->domain().lo;
    FK_CHECK(atEnd ? end > start : end < start);
    FK_CHECK_NEAR(arcLength(*actual, {std::min(start, end), std::max(start, end)}, 1e-13), requested, budget);
    // These controls have epsilon=0 and remain on a monotone polynomial
    // interval: physical endpoint distance is an independent length oracle.
    FK_CHECK_NEAR(distance(actual->point(end), actual->point(start)), requested, budget);
    FK_CHECK(writeBodyBinary(fixture.body) == before);
}
}

FK_TEST(SheetArcZeroSpeedCannotPublishShortReach) {
    // Baseline published a checkBody-valid result with length1, requested3.
    for (bool tensor : {false, true}) rejectsWrongReach(0, tensor);
}
FK_TEST(SheetArcRegularCurveCannotPublishExhaustedNewtonReach) {
    // This regular source/continuation has no zero derivative. Baseline
    // exhausted Newton and published length178.5340877756, requested3.
    for (bool tensor : {false, true}) rejectsWrongReach(std::ldexp(1., -36), tensor);
}
FK_TEST(SheetArcNaturalPositiveDistancesUsePhysicalReach) {
    for (bool tensor : {false, true}) {
        const auto fixture = curtain(0, 1, Frame3(), tensor);
        for (double requested : {.25, .5}) naturalReach(fixture, true, requested);
    }
}
FK_TEST(SheetArcOppositeDirectionAndPoseScaleKeepPhysicalReach) {
    const Frame3 pose(Vec3(12, -23, 7), Vec3(1, 2, .3), Vec3(2, -1, 0));
    for (bool tensor : {false, true})
        for (double scale : {.25, 2.5}) naturalReach(curtain(0, scale, pose, tensor), false, .5 * scale);
}
FK_TEST(SheetArcLinearAndCylinderControlsKeepTheirPhysicalContract) {
    const auto fixture = curtain(.25);
    const auto before = writeBodyBinary(fixture.body);
    const Body linear = extendSheet(fixture.body, {fixture.end}, .75, true);
    FK_CHECK(checkBody(linear).empty());
    const Vec3 start = fixture.base->point(1), direction = normalized(fixture.base->derivative(1));
    double reached = 0;
    for (VertexId v : linear.vertices()) reached = std::max(reached, dot(linear.vertex(v).point - start, direction));
    FK_CHECK_NEAR(reached, .75, operationBudget(fixture.body));
    FK_CHECK(writeBodyBinary(fixture.body) == before);

    const auto arc = std::make_shared<Circle<2>>(Vec2(), Vec2(1, 0), Vec2(0, 1), 2);
    const ProfileLoop chain{{{arc, {0, kHalfPi}, 17}}};
    const Body cylinder = makeSheetExtrusion(Frame3(), {chain}, 1);
    const auto cylinderBefore = writeBodyBinary(cylinder);
    // makeSheetExtrusion declares the two vertical edges first; index1 is
    // the terminal side of the sole source segment.
    const EdgeId terminal = cylinder.edges().at(1);
    const FinId sourceFin = cylinder.edge(terminal).forward.valid() ? cylinder.edge(terminal).forward : cylinder.edge(terminal).backward;
    const double u0 = cylinder.fin(sourceFin).pcurve->point(0).x();
    const Body result = extendSheet(cylinder, {terminal}, .5);
    FK_CHECK(checkBody(result).empty());
    double u1 = u0;
    for (FinId f : result.fins()) {
        FK_CHECK(bool(result.fin(f).pcurve));
        if (!result.fin(f).pcurve) continue;
        const auto r = result.edge(result.fin(f).edge).range;
        for (double t : {r.lo, r.hi}) u1 = std::max(u1, result.fin(f).pcurve->point(t).x());
    }
    FK_CHECK_NEAR(2 * (u1 - u0), .5, operationBudget(cylinder));
    FK_CHECK(writeBodyBinary(cylinder) == cylinderBefore);
}
FK_TEST(SheetArcCancellationDoesNotPublishOrMutateInput) {
    const auto fixture = curtain();
    const auto before = writeBodyBinary(fixture.body);
    BodyHistory history;
    const auto token = std::make_shared<std::atomic<bool>>(true);
    CancellationScope scope(token);
    bool cancelled = false;
    try { (void)extendSheet(fixture.body, {fixture.end}, .5, false, &history); }
    catch (const OperationCancelled &) { cancelled = true; }
    FK_CHECK(cancelled);
    emptyHistory(history);
    FK_CHECK(writeBodyBinary(fixture.body) == before);
}
