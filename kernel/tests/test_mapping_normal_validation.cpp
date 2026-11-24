#include "fk_body_check.h"
#include "fk_bspline.h"
#include "fk_body_io.h"
#include "fk_bspline_surface.h"
#include "fk_extrude.h"
#include "fk_history.h"
#include "fk_pcurve.h"
#include "fk_primitives.h"
#include "fk_revolve.h"
#include "fk_sheet.h"
#include "fk_sew.h"
#include "fk_surface_mapping.h"
#include "fk_test.h"

#include <cmath>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace ForgeCad::Kernel;
namespace {
constexpr double tolerance = 1e-6;

BSplineSurface normalCarrier(bool bump) {
    const std::vector<double> knots{0, 0, 0, 0, 46. / 256., 47. / 256.,
        48. / 256., 49. / 256., 50. / 256., 1, 1, 1, 1};
    std::vector<Vec3> poles;
    for (int i = 0; i < 9; ++i) for (int j = 0; j < 9; ++j)
        poles.emplace_back((knots[i + 1] + knots[i + 2] + knots[i + 3]) / 3.,
            (knots[j + 1] + knots[j + 2] + knots[j + 3]) / 3.,
            bump && i == 4 && j == 4 ? (9. / 4.) * 1e-4 : 0.);
    return BSplineSurface(3, 3, knots, knots, 9, 9, poles);
}

void carrierUnchanged(const BSplineSurface &a, const BSplineSurface &b) {
    FK_CHECK(a.uKnots() == b.uKnots() && a.vKnots() == b.vKnots());
    FK_CHECK(a.uDegree() == b.uDegree() && a.vDegree() == b.vDegree());
    FK_CHECK(a.uPoleCount() == b.uPoleCount() && a.vPoleCount() == b.vPoleCount());
    FK_CHECK(a.basis() == b.basis());
    for (int i = 0; i < b.uPoleCount(); ++i) for (int j = 0; j < b.vPoleCount(); ++j) {
        for (int k = 0; k < 3; ++k) FK_CHECK(a.pole(i, j)[k] == b.pole(i, j)[k]);
        FK_CHECK(a.weight(i, j) == b.weight(i, j));
    }
}

std::string normalHistoryStamp(const BodyHistory &history) {
    std::ostringstream out;
    out << history.operandCount();
    for (auto kind : {EntityKind::Vertex, EntityKind::Edge, EntityKind::Face}) {
        out << ':' << history.resultSize(kind);
        for (int i = 0; i < history.resultSize(kind); ++i) {
            out << '/' << history.isDeclared(kind, i);
            for (const auto &origin : history.origins(kind, i))
                out << ',' << origin.operand << ',' << int(origin.kind) << ',' << origin.index
                    << ',' << int(origin.relation);
        }
    }
    return out.str();
}

SurfaceWrapOptions normalOptions() {
    SurfaceWrapOptions options;
    options.anchor = {0, 0};
    options.tolerance = tolerance;
    return options;
}

void checkNormalResult(const Body &source, const Body &result, const BodyHistory &history, double deviation) {
    FK_CHECK(!result.isSheet() && result.faces().size() == source.faces().size());
    FK_CHECK(checkBody(result, {true, true}).empty());
    FK_CHECK(std::isfinite(deviation) && deviation >= 0 && deviation <= tolerance);
    FK_CHECK(history.operandCount() == 1);
    for (auto face : result.faces()) {
        const auto &origins = history.origins(EntityKind::Face, face.index);
        FK_CHECK(history.isDeclared(EntityKind::Face, face.index) && origins.size() == 1);
        if (origins.size() != 1) continue;
        FK_CHECK(origins.front().operand == 0 && origins.front().kind == EntityKind::Face
            && origins.front().relation == HistoryRelation::Modified);
        FK_CHECK(source.contains(FaceId{origins.front().index}));
    }
}
}

FK_TEST(MappingNormalRaisedSolidBumpRejectsBeforePublicationWithOrWithoutHistory) {
    const auto source = makeBox(Frame3(Vec3(0, 0, .002), Vec3(0, 0, 1), Vec3(1, 0, 0)), 1, 1, .001);
    const auto before = writeBodyBinary(source);
    const auto target = normalCarrier(true), oldTarget = target;
    // At c=3/16, B4(c)=2/3 and B4'(c)=0. The intended bottom height
    // is .002 + (4/9)*(9/4)*1e-4 = .0021, with the same +Z normal.
    FK_CHECK_NEAR(target.point(3. / 16., 3. / 16.).z() + .002, .0021, 1e-16);
    for (bool provideHistory : {true, false}) {
        auto history = sameIndexHistory(source, source);
        const auto oldHistory = normalHistoryStamp(history);
        double deviation = -123;
        bool rejected = false;
        try {
            (void)wrapBodyOnSurface(source, target, normalOptions(),
                provideHistory ? &history : nullptr, &deviation);
        } catch (const std::domain_error &error) {
            rejected = true;
            FK_CHECK(std::string(error.what()).find("residuo globale del carrier") != std::string::npos);
        }
        FK_CHECK(rejected);
        FK_CHECK(deviation == -123 && normalHistoryStamp(history) == oldHistory);
        FK_CHECK(writeBodyBinary(source) == before);
        carrierUnchanged(target, oldTarget);
    }
}

FK_TEST(MappingNormalFlatCarrierAcceptsPositiveAndNegativeHeights) {
    const auto target = normalCarrier(false), oldTarget = target;
    for (double z : {.002, -.003}) {
        const auto source = makeBox(Frame3(Vec3(0, 0, z), Vec3(0, 0, 1), Vec3(1, 0, 0)), 1, 1, .001);
        const auto before = writeBodyBinary(source);
        BodyHistory history;
        double deviation = -123;
        const auto result = wrapBodyOnSurface(source, target, normalOptions(), &history, &deviation);
        checkNormalResult(source, result, history, deviation);
        for (auto vertex : result.vertices()) {
            const auto &origins = history.origins(EntityKind::Vertex, vertex.index);
            FK_CHECK(history.isDeclared(EntityKind::Vertex, vertex.index) && origins.size() == 1);
            if (origins.size() != 1) continue;
            FK_CHECK(origins.front().operand == 0 && origins.front().kind == EntityKind::Vertex);
            const VertexId parent{origins.front().index};
            FK_CHECK(source.contains(parent));
            if (source.contains(parent))
                FK_CHECK(distance(result.vertex(vertex).point, source.vertex(parent).point) <= tolerance);
        }
        FK_CHECK(writeBodyBinary(result) == writeBodyBinary(wrapBodyOnSurface(source, target, normalOptions(), nullptr)));
        FK_CHECK(writeBodyBinary(source) == before);
        carrierUnchanged(target, oldTarget);
    }
}

FK_TEST(MappingNormalOrdinaryQuadraticWrapRemainsSuccessful) {
    // Existing public nonplanar wrap fixture, preserved without relaxing its
    // dimensions, operation tolerance or mandatory successful result.
    const std::vector<double> knots{0, 0, 0, 1, 1, 1};
    const double xy[]{-5, 0, 5}, square[]{25, -25, 25};
    std::vector<Vec3> poles;
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j)
        poles.emplace_back(xy[i], xy[j], .02 * (square[i] + square[j]));
    const BSplineSurface target(2, 2, knots, knots, 3, 3, poles), oldTarget = target;
    const auto source = makeBox(Frame3(Vec3(-.1, -.1, 0), Vec3(0, 0, 1), Vec3(1, 0, 0)), .2, .2, .02);
    const auto before = writeBodyBinary(source);
    auto options = normalOptions();
    options.anchor = {.5, .5};
    BodyHistory history;
    double deviation = -123;
    const auto result = wrapBodyOnSurface(source, target, options, &history, &deviation);
    checkNormalResult(source, result, history, deviation);
    FK_CHECK(writeBodyBinary(result) == writeBodyBinary(wrapBodyOnSurface(source, target, options, nullptr)));
    FK_CHECK(writeBodyBinary(source) == before);
    carrierUnchanged(target, oldTarget);
}

FK_TEST(MappingNormalTiltedSolidRejectsHiddenBumpButAcceptsFlatTarget) {
    const double angle = .001;
    const Frame3 frame(Vec3(0, 0, .002), Vec3(std::sin(angle), 0, std::cos(angle)),
        Vec3(std::cos(angle), 0, -std::sin(angle)));
    const auto source = makeBox(frame, .9, 1, .001);
    const auto before = writeBodyBinary(source);
    const auto target = normalCarrier(true);
    FK_CHECK_NEAR(target.point(3. / 16., 3. / 16.).z(), 1e-4, 1e-16);
    for (bool provideHistory : {true, false}) {
        auto history = sameIndexHistory(source, source);
        const auto stamp = normalHistoryStamp(history);
        double deviation = -123;
        bool rejected = false;
        try {
            (void)wrapBodyOnSurface(source, target, normalOptions(),
                provideHistory ? &history : nullptr, &deviation);
        } catch (const std::domain_error &error) {
            rejected = true;
            FK_CHECK(std::string(error.what()).find("residuo globale del carrier") != std::string::npos);
        }
        FK_CHECK(rejected);
        FK_CHECK(deviation == -123 && normalHistoryStamp(history) == stamp);
    }
    BodyHistory history;
    double deviation = -123;
    const auto flat = normalCarrier(false);
    const auto result = wrapBodyOnSurface(source, flat, normalOptions(), &history, &deviation);
    checkNormalResult(source, result, history, deviation);
    for (auto vertex : result.vertices()) {
        const auto &origins = history.origins(EntityKind::Vertex, vertex.index);
        FK_CHECK(origins.size() == 1);
        if (origins.size() != 1) continue;
        const VertexId parent{origins.front().index};
        FK_CHECK(source.contains(parent));
        if (source.contains(parent))
            FK_CHECK(distance(result.vertex(vertex).point, source.vertex(parent).point) <= tolerance);
    }
    FK_CHECK(writeBodyBinary(source) == before);
}

FK_TEST(MappingNormalSheetTouchingZeroRejectsHiddenBumpAndAcceptsFlatTarget) {
    constexpr double rate = 1. / 1024.;
    const double run = std::sqrt(1. - rate * rate);
    const auto frame = Frame3::fromAxes(Vec3(.4, .4, rate / 4), Vec3(run, 0, -rate),
        Vec3(0, 1, 0), Vec3(rate, 0, run));
    const auto source = makePlaneSheet(frame, .25);
    const auto before = writeBodyBinary(source);
    FK_CHECK(frame.toGlobal(Vec3(.25, 0, 0)).z() == 0);
    const auto target = normalCarrier(true);
    for (bool provideHistory : {false, true}) {
        auto history = sameIndexHistory(source, source);
        const auto stamp = normalHistoryStamp(history);
        double deviation = -123;
        bool rejected = false;
        try {
            (void)wrapBodyOnSurface(source, target, normalOptions(),
                provideHistory ? &history : nullptr, &deviation);
        } catch (const std::domain_error &error) {
            rejected = true;
            FK_CHECK(std::string(error.what()).find("residuo globale del carrier") != std::string::npos);
        }
        FK_CHECK(rejected);
        FK_CHECK(deviation == -123 && normalHistoryStamp(history) == stamp);
    }
    BodyHistory history;
    double deviation = -123;
    const auto flat = normalCarrier(false);
    const auto result = wrapBodyOnSurface(source, flat, normalOptions(), &history, &deviation);
    FK_CHECK(result.isSheet() && result.faces().size() == 1);
    FK_CHECK(checkBody(result, {true, true}).empty());
    FK_CHECK(std::isfinite(deviation) && deviation >= 0 && deviation <= tolerance);
    FK_CHECK(history.origins(EntityKind::Face, result.faces().front().index).size() == 1);
    FK_CHECK(writeBodyBinary(source) == before);
}


FK_TEST(MappingNormalVerticalSheetRejectsHiddenBumpAndAcceptsFlatTarget) {
    const auto box = makeBox(Frame3(Vec3(.25, .15, .001), Vec3(0, 0, 1), Vec3(1, 0, 0)), .01, .5, .0001);
    FaceId side;
    for (auto id : box.faces()) {
        const auto &plane = static_cast<const Plane &>(*box.face(id).surface);
        if (std::abs(plane.frame().zDir().x()) == 1 && plane.frame().origin().x() == .25)
            side = id;
    }
    if (!box.contains(side)) throw std::logic_error("vertical face missing");
    const auto source = facesAsSheet(box, {side});
    const auto before = writeBodyBinary(source);
    const auto uKnots = std::vector<double>{0, 0, 0, 0, 1, 1, 1, 1};
    const auto vKnots = normalCarrier(false).vKnots();
    for (bool bump : {false, true}) {
        std::vector<Vec3> poles;
        for (int i = 0; i < 4; ++i) for (int j = 0; j < 9; ++j)
            poles.emplace_back(i / 3., (vKnots[j + 1] + vKnots[j + 2] + vKnots[j + 3]) / 3.,
                bump && j == 4 ? 1.5e-4 : 0.);
        const BSplineSurface carrier(3, 3, uKnots, vKnots, 4, 9, poles);
        auto history = sameIndexHistory(source, source);
        const auto stamp = normalHistoryStamp(history);
        double deviation = -123.;
        bool rejected = false;
        try {
            const auto result = wrapBodyOnSurface(source, carrier, normalOptions(), &history, &deviation);
            FK_CHECK(result.isSheet() && result.faces().size() == 1);
            FK_CHECK(checkBody(result, {true, true}).empty());
            FK_CHECK(deviation >= 0 && deviation <= tolerance);
        } catch (const std::domain_error &error) {
            rejected = true;
            FK_CHECK(std::string(error.what()).find("residuo globale del carrier") != std::string::npos);
            FK_CHECK(deviation == -123. && normalHistoryStamp(history) == stamp);
        }
        FK_CHECK(rejected == bump);
    }
    FK_CHECK(writeBodyBinary(source) == before);
}

namespace {
enum class LaminaCase { Diagonal, Oblique, UncertainRank, Instanced };

void checkMappingNormalSheet(LaminaCase kind, bool variableWeights = false, int uDegree = 3) {
    const bool rotateUV = kind == LaminaCase::UncertainRank;
    const bool instanced = kind == LaminaCase::Instanced;
    const double tilt = kind == LaminaCase::Oblique ? 1e-7 : 0.;
    const Vec3 origin(.25, .15, .001);
    constexpr double height = .0001;
    const double length = rotateUV ? std::sqrt(.5) : .625;
    const auto sideNormal = rotateUV ? Vec3(1, -1, 0) : Vec3(.8, -.6, -tilt);
    const Frame3 boxFrame(origin, Vec3(.8 * tilt, -.6 * tilt, 1), sideNormal);
    const auto box = makeBox(boxFrame, .01, length, height);
    FaceId side;
    for (auto id : box.faces()) {
        const auto &plane = static_cast<const Plane &>(*box.face(id).surface);
        const auto &frame = plane.frame();
        if (std::abs(dot(frame.zDir(), boxFrame.xDir())) > .99
            && std::abs(dot(frame.zDir(), origin - frame.origin())) < 1e-10) {
            side = id;
            break;
        }
    }
    if (!box.contains(side)) throw std::logic_error("diagonal lamina face missing");
    auto source = facesAsSheet(box, {side});
    auto &face = source.face(source.faces().front());
    if (rotateUV) {
        const auto &frame = static_cast<const Plane &>(*face.surface).frame();
        // Change the parameter basis of the vertical sheet without moving
        // its boundary. Both new axes project onto the same XY line.
        face.surface = std::make_shared<Plane>(Frame3(frame.origin(), frame.zDir(),
            boxFrame.yDir() + boxFrame.zDir()));
        for (auto loop : face.loops)
            for (auto fin : source.loopFins(loop)) source.fin(fin).pcurve.reset();
        FK_CHECK(computePCurves(source) == 0);
    }
    const auto before = writeBodyBinary(source);
    const auto &frame = static_cast<const Plane &>(*face.surface).frame();
    if (rotateUV) {
        const auto x = frame.xDir(), y = frame.yDir();
        FK_CHECK(x.x() != 0. && y.x() != 0. && x.x() == x.y() && y.x() == y.y());
    }
    std::vector<double> uKnots(uDegree + 1, 0.);
    uKnots.insert(uKnots.end(), uDegree + 1, 1.);
    constexpr double peak = 3. / 16., step = 1. / 512.;
    const auto vKnots = rotateUV
        ? std::vector<double>{0, 0, 0, 0, peak - 2 * step, peak - step, peak, peak + step, peak + 2 * step, 1, 1, 1, 1}
        : normalCarrier(false).vKnots();
    const Vec3 midHeight = origin + .5 * height * boxFrame.zDir();
    const Vec3 translation = instanced ? Vec3(.125, -.25, .5) : Vec3();
    const auto placement = Transform3::translation(translation);
    for (bool bump : {false, true}) {
        std::vector<Vec3> poles;
        std::vector<double> weights;
        for (int i = 0; i <= uDegree; ++i) for (int j = 0; j < 9; ++j) {
            double x = double(i) / uDegree;
            if (variableWeights) {
                const double weight = 1. + .5 * x;
                weights.push_back(weight);
                // X numerator=u*(1+.5*u), W=1+.5*u: X/W=u.
                x = (x + .5 * i * (i - 1) / (uDegree * (uDegree - 1))) / weight;
            }
            poles.emplace_back(x, (vKnots[j + 1] + vKnots[j + 2] + vKnots[j + 3]) / 3.,
                bump && j == 4 ? 1.5e-4 : 0.);
        }
        const auto base = std::make_shared<BSplineSurface>(uDegree, 3, uKnots, vKnots, uDegree + 1, 9, poles, weights);
        const auto baseBefore = *base;
        const auto carrier = instanced ? BSplineSurface(base, placement) : *base;
        const auto carrierBefore = carrier;
        Vec3 jet[4];
        carrier.evaluate(0, 0, 1, jet);
        const double along = (peak * norm(jet[1]) - midHeight.y()) / boxFrame.yDir().y();
        FK_CHECK(along > 0 && along < length);
        const Vec3 probe = midHeight + along * boxFrame.yDir();
        const auto uv = frame.toLocal(probe);
        const double u = probe.x() / norm(jet[2]), v = probe.y() / norm(jet[1]);
        const auto expected = carrier.point(u, v) + probe.z() * carrier.normal(u, v);
        FK_CHECK_NEAR(expected.z() - probe.z() - translation.z(), bump ? 1e-4 : 0., 1e-15);

        auto history = sameIndexHistory(source, source);
        const auto stamp = normalHistoryStamp(history);
        double deviation = -123.;
        bool rejected = false;
        try {
            const auto result = wrapBodyOnSurface(source, carrier, normalOptions(), &history, &deviation);
            FK_CHECK(result.isSheet() && result.faces().size() == 1);
            FK_CHECK(checkBody(result, {true, true}).empty());
            FK_CHECK(std::isfinite(deviation));
            FK_CHECK(deviation >= 0 && deviation <= tolerance);
            if (result.faces().size() == 1) {
                const auto &image = *result.face(result.faces().front()).surface;
                const auto *spline = dynamic_cast<const BSplineSurface *>(&image);
                FK_CHECK(spline && !spline->basis());
                FK_CHECK(distance(image.point(uv.x(), uv.y()), expected) <= deviation + 1e-14);
            }
        } catch (const std::domain_error &error) {
            rejected = true;
            FK_CHECK(std::string(error.what()).find("residuo globale del carrier") != std::string::npos);
            FK_CHECK(deviation == -123. && normalHistoryStamp(history) == stamp);
        }
        FK_CHECK(rejected == bump);
        carrierUnchanged(*base, baseBefore);
        carrierUnchanged(carrier, carrierBefore);
    }
    FK_CHECK(writeBodyBinary(source) == before);
}
}

FK_TEST(MappingNormalDiagonalRankOneRejectsHiddenBumpAndAcceptsFlatTarget) {
    checkMappingNormalSheet(LaminaCase::Diagonal);
}

FK_TEST(MappingNormalObliqueRankTwoRejectsHiddenBumpAndAcceptsFlatTarget) {
    checkMappingNormalSheet(LaminaCase::Oblique);
}

FK_TEST(MappingNormalObliqueUncertainRankRejectsHiddenBumpAndAcceptsFlatTarget) {
    checkMappingNormalSheet(LaminaCase::UncertainRank);
}

FK_TEST(MappingNormalInstancedTargetRejectsHiddenBumpAndAcceptsFlatTarget) {
    checkMappingNormalSheet(LaminaCase::Instanced);
}

FK_TEST(MappingNormalVariableWeightDiagonalRankOneRejectsHiddenBumpAndAcceptsFlatTarget) {
    checkMappingNormalSheet(LaminaCase::Diagonal, true);
}

FK_TEST(MappingNormalVariableWeightInstancedTargetRejectsHiddenBumpAndAcceptsFlatTarget) {
    checkMappingNormalSheet(LaminaCase::Instanced, true);
}

FK_TEST(MappingNormalVariableWeightObliqueRankTwoRejectsHiddenBumpAndAcceptsFlatTarget) {
    checkMappingNormalSheet(LaminaCase::Oblique, true);
}

FK_TEST(MappingNormalVariableWeightObliqueUncertainRankRejectsHiddenBumpAndAcceptsFlatTarget) {
    checkMappingNormalSheet(LaminaCase::UncertainRank, true);
}

FK_TEST(MappingNormalMixedDegreesRejectHiddenBumpAndAcceptFlatTarget) {
    checkMappingNormalSheet(LaminaCase::Diagonal, false, 1);
    checkMappingNormalSheet(LaminaCase::Diagonal, false, 4);
    checkMappingNormalSheet(LaminaCase::Oblique, true, 5);
}

namespace {
enum class ExtrusionProfile { Open, Trimmed, Closed, Reflected };

void checkMappingNormalExtrusion(ExtrusionProfile kind) {
    const bool closed = kind == ExtrusionProfile::Closed;
    const bool reflected = kind == ExtrusionProfile::Reflected;
    // The narrow generator bump lies between the former global sample sites.
    const Vec3 origin(.15, .25, .001);
    const Frame3 boxFrame(origin, Vec3(0, 0, 1), Vec3(.6, -.8, 0));
    const auto box = makeBox(boxFrame, .01, .625, .0001);
    FaceId side;
    for (auto id : box.faces()) {
        const auto &frame = static_cast<const Plane &>(*box.face(id).surface).frame();
        if (std::abs(dot(frame.zDir(), boxFrame.xDir())) > .99
            && std::abs(dot(frame.zDir(), origin - frame.origin())) < 1e-10)
            side = id;
    }
    if (!box.contains(side)) throw std::logic_error("extrusion lamina face missing");
    const auto source = facesAsSheet(box, {side});
    const auto before = writeBodyBinary(source);
    const auto &frame = static_cast<const Plane &>(*source.face(source.faces().front()).surface).frame();
    constexpr double center = 3. / 16., step = 1. / 512.;
    const double straightEnd = closed ? .5 : 1.;
    const std::vector<double> initialKnots{0, 0, 0, 0, center - 2 * step, center - step,
        center, center + step, center + 2 * step, straightEnd, straightEnd, straightEnd, straightEnd};
    auto options = normalOptions();
    if (closed) options.shift = Vec2(-.2, 0.);
    for (bool bump : {false, true}) {
        auto knots = initialKnots;
        std::vector<Vec3> poles;
        for (int i = 0; i < 9; ++i)
            poles.emplace_back((knots[i + 1] + knots[i + 2] + knots[i + 3]) / 3.,
                0, bump && i == 4 ? 1.5e-4 : 0.);
        if (closed) {
            // Keep the mapped neighbourhood exactly straight through the seam
            // in the control. Join the straight pieces away from the lamina.
            knots.pop_back();
            for (double knot : {.5625, .6875, .75}) knots.insert(knots.end(), 3, knot);
            knots.insert(knots.end(), 4, 1.);
            const double tangent = 1. / 48.;
            const std::vector<Vec3> closingPoles{
                {.5 + tangent, 0, 0}, {.6, 0, -.05}, {.6, 0, -.1},
                {.6, 0, -.15}, {-.35, 0, -.15}, {-.35, 0, -.1},
                {-.35, 0, -.05}, {-.25 - tangent, 0, 0}, {-.25, 0, 0},
                {-1. / 6., 0, 0}, {-1. / 12., 0, 0}, {0, 0, 0}};
            poles.insert(poles.end(), closingPoles.begin(), closingPoles.end());
        }
        const auto generator = std::make_shared<BSplineCurve<3>>(3, knots, poles);
        CurvePtr<3> profile = generator;
        if (kind == ExtrusionProfile::Trimmed)
            profile = std::make_shared<TrimmedCurve<3>>(profile, 0., .9);
        if (reflected) {
            const auto reflection = Transform3::reflection(Vec3(.125, 0, 0), Vec3(1, 0, 0));
            profile = std::make_shared<TransformedCurve>(profile, reflection);
        }
        const ExtrusionSurface target(profile, Vec3(0, 1, 0));
        Vec3 jet[4];
        target.evaluate(0, 0, 1, jet);
        const double su = norm(jet[2]), sv = norm(jet[1]);
        const auto mappedU = [&](const Vec3 &p) { return (p.x() + options.shift.x()) / su; };
        const auto probeAt = [&](double u) {
            return origin + ((u * su - origin.x() - options.shift.x()) / boxFrame.yDir().x())
                * boxFrame.yDir() + .00005 * boxFrame.zDir();
        };
        const auto expectedAt = [&](const Vec3 &p) {
            return target.point(mappedU(p), p.y() / sv) + p.z() * target.normal(mappedU(p), p.y() / sv);
        };
        if (closed) {
            FK_CHECK(!generator->isPeriodic());
            FK_CHECK(distance(generator->point(0.), generator->point(1.)) == 0.);
            FK_CHECK(target.isUPeriodic() && target.uPeriod() == 1.);
            FK_CHECK(mappedU(origin) < -.025);
            FK_CHECK(mappedU(origin + .625 * boxFrame.yDir()) > center + 2 * step);
            FK_CHECK(mappedU(origin + .625 * boxFrame.yDir()) < straightEnd);
            FK_CHECK(distance(target.point(-.025, .5), target.point(.975, .5)) <= 1e-15);
        }
        const auto probe = probeAt(center);
        const auto expected = expectedAt(probe);
        if (reflected) {
            // Reflection reverses the natural normal while preserving the
            // generator's Z bump. The normal offset therefore points down.
            FK_CHECK_NEAR(expected.x(), .25 - center, 1e-15);
            FK_CHECK_NEAR(expected.z() + probe.z(), bump ? 1e-4 : 0., 1e-15);
        } else {
            FK_CHECK_NEAR(expected.z() - probe.z(), bump ? 1e-4 : 0., 1e-15);
        }
        for (bool provideHistory : {false, true}) {
            auto history = sameIndexHistory(source, source);
            const auto stamp = normalHistoryStamp(history);
            double deviation = -123.;
            bool rejected = false;
            try {
                const auto result = wrapBodyOnSurface(source, target, options,
                    provideHistory ? &history : nullptr, &deviation);
                FK_CHECK(result.isSheet() && result.faces().size() == 1);
                FK_CHECK(checkBody(result, {true, true}).empty());
                FK_CHECK(std::isfinite(deviation) && deviation >= 0 && deviation <= tolerance);
                if (result.faces().size() == 1) {
                    const auto &image = *result.face(result.faces().front()).surface;
                    const auto checkPoint = [&](const Vec3 &p, const Vec3 &targetPoint) {
                        const auto uv = frame.toLocal(p);
                        FK_CHECK(distance(image.point(uv.x(), uv.y()), targetPoint) <= deviation + 1e-14);
                    };
                    checkPoint(probe, expected);
                    if (closed) {
                        for (double u : {-.025, 0., .025}) {
                            const auto seamProbe = probeAt(u);
                            checkPoint(seamProbe, expectedAt(seamProbe));
                        }
                    }
                }
                if (provideHistory) {
                    FK_CHECK(history.operandCount() == 1);
                    for (auto face : result.faces()) {
                        const auto &origins = history.origins(EntityKind::Face, face.index);
                        FK_CHECK(history.isDeclared(EntityKind::Face, face.index) && origins.size() == 1);
                        if (origins.size() == 1)
                            FK_CHECK(origins.front().operand == 0 && origins.front().kind == EntityKind::Face
                                && origins.front().index == source.faces().front().index
                                && origins.front().relation == HistoryRelation::Modified);
                    }
                }
            } catch (const std::domain_error &error) {
                rejected = true;
                FK_CHECK(std::string(error.what()).find("residuo globale del carrier") != std::string::npos);
            }
            FK_CHECK(rejected == bump);
            if (rejected) FK_CHECK(deviation == -123. && normalHistoryStamp(history) == stamp);
            if (!provideHistory) FK_CHECK(normalHistoryStamp(history) == stamp);
            FK_CHECK(writeBodyBinary(source) == before);
            FK_CHECK(target.curve() == profile && generator->degree() == 3 && generator->knots() == knots);
            FK_CHECK(generator->weights().empty() && generator->poles().size() == poles.size());
            for (std::size_t i = 0; i < poles.size(); ++i)
                for (int axis = 0; axis < 3; ++axis)
                    FK_CHECK(generator->poles()[i][axis] == poles[i][axis]);
            for (int axis = 0; axis < 3; ++axis)
                FK_CHECK(target.direction()[axis] == Vec3(0, 1, 0)[axis]);
        }
    }
}
}

FK_TEST(MappingNormalExtrusionRejectsHiddenBumpAndAcceptsFlatTarget) {
    checkMappingNormalExtrusion(ExtrusionProfile::Open);
    checkMappingNormalExtrusion(ExtrusionProfile::Trimmed);
}

FK_TEST(MappingNormalClosedExtrusionRejectsHiddenBumpAndAcceptsSeamCrossing) {
    checkMappingNormalExtrusion(ExtrusionProfile::Closed);
}

FK_TEST(MappingNormalReflectedExtrusionRejectsHiddenBumpAndAcceptsFlatTarget) {
    checkMappingNormalExtrusion(ExtrusionProfile::Reflected);
}

namespace {
Body makeMappingLamina(const Frame3 &boxFrame, double length, double height) {
    const auto box = makeBox(boxFrame, .01, length, height);
    FaceId side;
    for (auto id : box.faces()) {
        const auto &frame = static_cast<const Plane &>(*box.face(id).surface).frame();
        if (std::abs(dot(frame.zDir(), boxFrame.xDir())) > .99
            && std::abs(dot(frame.zDir(), boxFrame.origin() - frame.origin())) < 1e-10)
            side = id;
    }
    if (!box.contains(side)) throw std::logic_error("mapping lamina face missing");
    return facesAsSheet(box, {side});
}

enum class ConicPlacement { Direct, Feature, Transformed };

void checkMappingNormalConicExtrusion(bool elliptic, ConicPlacement placement = ConicPlacement::Direct) {
    const bool fromFeature = placement == ConicPlacement::Feature;
    const bool transformed = placement == ConicPlacement::Transformed;
    const double radiusZ = elliptic ? .7 : 1.;
    const double widthX = elliptic ? (transformed ? .004 : .002) : .01;
    const double signedRadiusX = transformed ? (elliptic ? 1.5 : -1.) : 1.;
    const double centerX = transformed && !elliptic ? .25 : 0.;
    const double length = widthX / .8;
    constexpr double height = .0001;
    const double sourceZ = transformed ? (elliptic ? .01 : .001) : .1;
    const Vec3 origin(-.72 * widthX, .05, sourceZ);
    const Frame3 boxFrame(origin, Vec3(0, 0, 1), Vec3(.6, -.8, 0));
    const auto source = makeMappingLamina(boxFrame, length, height);
    const auto before = writeBodyBinary(source);
    const auto &frame = static_cast<const Plane &>(*source.face(source.faces().front()).surface).frame();
    SurfacePtr target;
    if (fromFeature) {
        CurvePtr<2> basis;
        if (elliptic) basis = std::make_shared<Ellipse<2>>(makeEllipse(Vec2(), 1., radiusZ));
        else basis = std::make_shared<Circle<2>>(makeCircle(Vec2(), 1.));
        const auto arc = std::make_shared<TrimmedCurve<2>>(basis, -.5, .5);
        const auto chordStart = arc->point(.5), chordEnd = arc->point(-.5);
        const auto chord = std::make_shared<Line<2>>(chordStart, chordEnd - chordStart);
        ProfileRegion region;
        region.outer.segments = {{arc, arc->domain()}, {chord, {0., distance(chordStart, chordEnd)}}};
        const auto tool = makeExtrusion(Frame3(), region, 1.);
        FK_CHECK(checkBody(tool, {true, true}).empty());
        for (auto id : tool.faces()) {
            const auto &surface = tool.face(id).surface;
            if (surface->type() == (elliptic ? SurfaceType::Extrusion : SurfaceType::Cylinder)) {
                FK_CHECK(!target);
                target = surface;
            }
        }
        if (!target) throw std::logic_error("conic side face missing");
        if (elliptic) {
            const auto &profile = static_cast<const ExtrusionSurface &>(*target).curve();
            const auto *trim = dynamic_cast<const TrimmedCurve<3> *>(profile.get());
            FK_CHECK(trim && trim->basis()->type() == CurveType::Ellipse);
            FK_CHECK(target->uDomain().lo == -.5 && target->uDomain().hi == .5);
        }
    } else {
        const Vec3 xAxis(1, 0, 0), zAxis(0, 0, 1);
        CurvePtr<3> profile;
        if (elliptic) profile = std::make_shared<Ellipse<3>>(Vec3(), xAxis, zAxis, 1., radiusZ);
        else profile = std::make_shared<Circle<3>>(Vec3(), xAxis, zAxis, 1.);
        if (transformed) {
            const double matrix[3][3]{{signedRadiusX, 0, 0}, {0, 1, 0}, {0, 0, 1}};
            profile = std::make_shared<TransformedCurve>(profile, Transform3::fromParts(matrix, Vec3(centerX, 0, 0)));
        }
        target = std::make_shared<ExtrusionSurface>(profile, Vec3(0, 1, 0));
    }
    if (fromFeature && elliptic) FK_CHECK(!target->isUPeriodic());
    else FK_CHECK(target->isUPeriodic() && target->uPeriod() == kTwoPi);
    FK_CHECK(origin.x() < 0. && (origin + length * boxFrame.yDir()).x() > 0.);

    for (bool provideHistory : {false, true}) {
        auto history = sameIndexHistory(source, source);
        const auto stamp = normalHistoryStamp(history);
        double deviation = -123.;
        const auto result = wrapBodyOnSurface(source, *target, normalOptions(),
            provideHistory ? &history : nullptr, &deviation);
        FK_CHECK(result.isSheet() && result.faces().size() == 1);
        FK_CHECK(checkBody(result, {true, true}).empty());
        FK_CHECK(std::isfinite(deviation) && deviation >= 0. && deviation <= tolerance);
        if (provideHistory && result.faces().size() == 1) {
            FK_CHECK(history.operandCount() == 1);
            const auto &origins = history.origins(EntityKind::Face, result.faces().front().index);
            FK_CHECK(origins.size() == 1);
            if (origins.size() == 1)
                FK_CHECK(origins.front().operand == 0 && origins.front().kind == EntityKind::Face
                    && origins.front().index == source.faces().front().index
                    && origins.front().relation == HistoryRelation::Modified);
        } else if (!provideHistory) {
            FK_CHECK(normalHistoryStamp(history) == stamp);
        }
        FK_CHECK(writeBodyBinary(source) == before);
        if (result.faces().size() != 1) continue;

        // The direct and transformed fixtures have an interior witness to the
        // former underestimate. The other probes bracket the zero angle.
        const auto &image = *result.face(result.faces().front()).surface;
        const double witnessSample = transformed ? (elliptic ? 309. : 310.) : (elliptic ? 190. : 184.);
        const double witness = witnessSample / 500.;
        for (double fraction : {witness, .65, .72, .8}) {
            const auto p = origin + (length * fraction) * boxFrame.yDir() + (.5 * height) * boxFrame.zDir();
            const double u = p.x() / radiusZ;
            const double c = std::cos(u), s = std::sin(u);
            const auto base = fromFeature ? Vec3(c, radiusZ * s, p.y())
                : Vec3(centerX + signedRadiusX * c, p.y(), radiusZ * s);
            const auto normal = normalized(fromFeature ? Vec3(radiusZ * c, s, 0)
                : Vec3(-radiusZ * c, 0, -signedRadiusX * s));
            const auto expected = base + p.z() * normal;
            const auto uv = frame.toLocal(p);
            const auto actual = image.point(uv.x(), uv.y());
            FK_CHECK(distance(actual, expected) <= deviation + 1e-13);
            if (transformed) {
                const auto radial = base - Vec3(centerX, p.y(), 0.);
                // Reflection places the natural offset outside the circle.
                FK_CHECK((elliptic ? -1. : 1.) * dot(actual - base, radial) > 0.);
            }
        }
    }
}
}

FK_TEST(MappingNormalCircularExtrusionBoundsDeviationAcrossSeam) {
    checkMappingNormalConicExtrusion(false);
}

FK_TEST(MappingNormalEllipticExtrusionBoundsDeviationAcrossSeam) {
    checkMappingNormalConicExtrusion(true);
}

FK_TEST(MappingNormalConicExtrusionFacesFromFeature) {
    checkMappingNormalConicExtrusion(false, ConicPlacement::Feature);
    checkMappingNormalConicExtrusion(true, ConicPlacement::Feature);
}


FK_TEST(MappingNormalReflectedCircleExtrusionBoundsDeviation) {
    checkMappingNormalConicExtrusion(false, ConicPlacement::Transformed);
}

FK_TEST(MappingNormalScaledEllipseExtrusionBoundsDeviation) {
    checkMappingNormalConicExtrusion(true, ConicPlacement::Transformed);
}

namespace {
void checkMappingNormalLineRevolution(bool inclined) {
    const double widthX = inclined ? .012 : .009;
    const double z = inclined ? .1 : .001;
    const double length = widthX / .8;
    constexpr double seam = .19, height = .0001;
    const Vec3 origin(-seam * widthX, .05, z);
    const Frame3 boxFrame(origin, Vec3(0, 0, 1), Vec3(.6, -.8, 0));
    const auto source = makeMappingLamina(boxFrame, length, height);
    const auto before = writeBodyBinary(source);
    const auto &frame = static_cast<const Plane &>(*source.face(source.faces().front()).surface).frame();
    const double slope = inclined ? .25 : 0.;
    const double speed = std::hypot(slope, 1.);
    const auto meridian = std::make_shared<Line<3>>(Vec3(1, 0, 0), Vec3(slope, 0, 1));
    const RevolutionSurface target(meridian, Vec3(), Vec3(0, 0, 1));
    FK_CHECK(target.isUPeriodic() && target.uPeriod() == kTwoPi);
    FK_CHECK(origin.x() < 0. && (origin + length * boxFrame.yDir()).x() > 0.);

    for (bool provideHistory : {false, true}) {
        auto history = sameIndexHistory(source, source);
        const auto stamp = normalHistoryStamp(history);
        double deviation = -123.;
        const auto result = wrapBodyOnSurface(source, target, normalOptions(),
            provideHistory ? &history : nullptr, &deviation);
        FK_CHECK(result.isSheet() && result.faces().size() == 1);
        FK_CHECK(checkBody(result, {true, true}).empty());
        FK_CHECK(std::isfinite(deviation) && deviation >= 0. && deviation <= tolerance);
        FK_CHECK(writeBodyBinary(source) == before);
        if (!provideHistory) FK_CHECK(normalHistoryStamp(history) == stamp);
        if (result.faces().size() != 1) continue;
        if (provideHistory) {
            const auto &origins = history.origins(EntityKind::Face, result.faces().front().index);
            FK_CHECK(history.operandCount() == 1 && origins.size() == 1);
            if (origins.size() == 1)
                FK_CHECK(origins.front().operand == 0 && origins.front().kind == EntityKind::Face
                    && origins.front().index == source.faces().front().index
                    && origins.front().relation == HistoryRelation::Modified);
        }

        const auto &image = *result.face(result.faces().front()).surface;
        const double witness = (inclined ? 188. : 316.) / 500.;
        for (double fraction : {.1, seam, .3, witness}) {
            const auto p = origin + (length * fraction) * boxFrame.yDir() + (.5 * height) * boxFrame.zDir();
            const auto uv = frame.toLocal(p);
            const Vec3 radial(std::cos(p.x()), std::sin(p.x()), 0.);
            const double axial = p.y() / speed;
            const auto base = (1. + slope * axial) * radial + Vec3(0., 0., axial);
            const auto normal = (radial - Vec3(0., 0., slope)) / speed;
            const auto actual = image.point(uv.x(), uv.y());
            FK_CHECK(distance(actual, base + p.z() * normal) <= deviation + 1e-13);
            FK_CHECK(dot(actual - base, normal) > 0.);
        }
    }
}
}

FK_TEST(MappingNormalAxialLineRevolutionBoundsDeviationAcrossSeam) {
    checkMappingNormalLineRevolution(false);
}

FK_TEST(MappingNormalInclinedLineRevolutionBoundsDeviationAcrossSeam) {
    checkMappingNormalLineRevolution(true);
}

FK_TEST(MappingNormalTrimmedEllipseFaceFromRevolutionFeature) {
    constexpr double radialRadius = .2, axialRadius = .15;
    const auto ellipse = std::make_shared<Ellipse<2>>(makeEllipse(Vec2(1., 0.), radialRadius, axialRadius));
    const auto arc = std::make_shared<TrimmedCurve<2>>(ellipse, 0., kPi);
    const auto chord = std::make_shared<Line<2>>(arc->point(kPi), arc->point(0.) - arc->point(kPi));
    ProfileRegion region;
    region.outer.segments = {{arc, arc->domain()}, {chord, {0., 2. * radialRadius}}};
    const auto tool = makeRevolution(Frame3(), region);
    FK_CHECK(checkBody(tool, {true, true}).empty());
    const RevolutionSurface *target = nullptr;
    for (auto id : tool.faces()) {
        if (const auto *side = dynamic_cast<const RevolutionSurface *>(tool.face(id).surface.get())) {
            FK_CHECK(!target);
            target = side;
        }
    }
    if (!target) throw std::logic_error("elliptic revolution face missing");
    const auto *trim = dynamic_cast<const TrimmedCurve<3> *>(target->meridian().get());
    FK_CHECK(trim && trim->basis()->type() == CurveType::Ellipse);
    FK_CHECK(target->vDomain().lo == 0. && target->vDomain().hi == kPi);

    constexpr double widthX = .009, length = widthX / .8, height = .0001;
    const Vec3 origin(-.34 * widthX, .05, .02);
    const Frame3 boxFrame(origin, Vec3(0, 0, 1), Vec3(.6, -.8, 0));
    const auto source = makeMappingLamina(boxFrame, length, height);
    const auto &frame = static_cast<const Plane &>(*source.face(source.faces().front()).surface).frame();
    for (bool provideHistory : {false, true}) {
        BodyHistory history;
        double deviation = -123.;
        const auto result = wrapBodyOnSurface(source, *target, normalOptions(),
            provideHistory ? &history : nullptr, &deviation);
        FK_CHECK(result.isSheet() && result.faces().size() == 1);
        FK_CHECK(checkBody(result, {true, true}).empty());
        FK_CHECK(std::isfinite(deviation) && deviation >= 0. && deviation <= tolerance);
        if (result.faces().size() != 1) continue;
        const auto face = result.faces().front();
        if (provideHistory)
            FK_CHECK(history.origins(EntityKind::Face, face.index).size() == 1);
        for (double fraction : {0., .34, .67, 1.}) {
            const auto p = origin + (length * fraction) * boxFrame.yDir() + (.5 * height) * boxFrame.zDir();
            const auto uv = frame.toLocal(p);
            const double u = p.x() / (1. + radialRadius), v = p.y() / axialRadius;
            const Vec3 radial(std::cos(u), std::sin(u), 0.);
            const double dr = radialRadius * std::cos(v), dz = axialRadius * std::sin(v);
            const auto base = (1. + dr) * radial + Vec3(0., 0., dz);
            const auto normal = normalized(axialRadius * std::cos(v) * radial + Vec3(0., 0., radialRadius * std::sin(v)));
            FK_CHECK(distance(result.face(face).surface->point(uv.x(), uv.y()), base + p.z() * normal)
                <= deviation + 1e-13);
        }
    }
}
