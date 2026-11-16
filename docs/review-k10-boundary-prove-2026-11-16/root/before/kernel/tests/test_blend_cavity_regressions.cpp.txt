#include "fk_test.h"
#include "fk_blend.h"
#include "fk_blend_contour.h"
#include "fk_body_io.h"
#include "fk_body_check.h"
#include "fk_bspline_surface.h"
#include "fk_history.h"
#include "fk_tessellate.h"
#include "fk_transform.h"
#include "fk_cancel.h"
#include <sstream>
#include <algorithm>
#include <fstream>
#include <iterator>
#include <set>
using namespace ForgeCad::Kernel;
namespace {
Body cavityFixture() {
    std::ifstream file(std::string(FORGECAD_SOURCE_DIR) + "/kernel/tests/data/blend_cavity_loft.body", std::ios::binary);
    if (!file) throw std::runtime_error("fixture cavita' mancante");
    return readBodyBinary(std::string(std::istreambuf_iterator<char>(file), {}));
}
void checkCavityVariant(int seed, bool reverse, double scale, bool pose) {
    const Body original = cavityFixture();
    const std::string originalBytes = writeBodyBinary(original);
    Transform3 transform = Transform3::scaling({}, scale);
    if (pose)
        transform = Transform3::translation({7, -4, 2}) * Transform3::rotation({}, Vec3(1, 2, 3), .42) * transform;
    // transformBody explicitly preserves topology indices; the seed therefore
    // retains its original identity instead of being rebound by proximity.
    const Body body = scale != 1 || pose ? transformBody(original, transform) : original;
    const std::string before = writeBodyBinary(body);
    auto contour = blendCavityContour(body, EdgeId(seed));
    std::set<int> ids;
    for (auto edge : contour) ids.insert(edge.index);
    FK_CHECK((ids == std::set<int>{3, 10, 11, 12, 17, 18, 19, 25}));
    if (reverse) std::reverse(contour.begin(), contour.end());
    const double radius = .5 * scale;
    BodyHistory history;
    const Body result = blendEdges(body, contour, radius, false, &history, false);
    FK_CHECK(writeBodyBinary(body) == before);
    FK_CHECK(writeBodyBinary(original) == originalBytes);
    FK_CHECK(result.faces().size() == 18);
    FK_CHECK(checkBody(result, {true, true}).empty());
    FK_CHECK(checkBody(readBodyBinary(writeBodyBinary(result)), {true, true}).empty());
    for (auto fin : result.fins()) FK_CHECK(bool(result.fin(fin).pcurve));
    int generated = 0;
    for (auto face : result.faces()) {
        FK_CHECK(history.isDeclared(EntityKind::Face, face.index));
        bool blend = false;
        for (const auto &link : history.origins(EntityKind::Face, face.index))
            blend = blend || (link.kind == EntityKind::Edge && link.relation == HistoryRelation::Generated);
        if (!blend) continue;
        ++generated;
        const auto *surface = dynamic_cast<const BSplineSurface *>(result.face(face).surface.get());
        FK_CHECK(surface && surface->vDegree() == 2);
        if (!surface) continue;
        for (double fraction : {.2, .5, .8}) {
            const double u = surface->uDomain().lo + fraction * surface->uDomain().length();
            const Vec3 p = surface->point(u, 0), q = surface->point(u, .5), r = surface->point(u, 1);
            const Vec3 a = q - p, b = r - p, n = cross(a, b);
            FK_CHECK(norm(n) > 0);
            const Vec3 center = p + (squaredNorm(a) * cross(b, n) + squaredNorm(b) * cross(n, a)) / (2 * squaredNorm(n));
            for (double v : {0., .25, .5, .75, 1.})
                FK_CHECK_NEAR(distance(center, surface->point(u, v)), radius, 1e-7);
        }
    }
    FK_CHECK(generated == 10);
    TessellationOptions options;
    // Scale display deflection with the fixture, while the geometric radius
    // assertion above keeps the original absolute 1e-7 mm tolerance.
    options.deflection = .05 * scale;
    options.threads = 2;
    FK_CHECK(tessellate(result, options).failedFaces == 0);
}
}
FK_TEST(BlendCavitySmallRadiusKeepsForwardContour) { checkCavityVariant(10, false, 1, false); }
FK_TEST(BlendCavitySmallRadiusKeepsDifferentSeed) { checkCavityVariant(17, false, 1, false); }
FK_TEST(BlendCavitySmallRadiusKeepsRigidPose) { checkCavityVariant(3, true, 1, true); }
FK_TEST(BlendCavitySmallRadiusKeepsScaledGeometry) { checkCavityVariant(19, true, 2.5, false); }

namespace {
std::string cavityHistorySnapshot(const BodyHistory &history) {
    std::ostringstream out;
    out << history.operandCount();
    for (auto kind : {EntityKind::Vertex, EntityKind::Edge, EntityKind::Face}) {
        out << '/' << history.resultSize(kind);
        for (int operand = 0; operand < history.operandCount(); ++operand)
            out << ':' << history.operandSize(operand, kind);
        for (int i = 0; i < history.resultSize(kind); ++i) {
            out << '|' << history.isDeclared(kind, i);
            for (const auto &link : history.origins(kind, i))
                out << ',' << link.operand << ',' << int(link.kind) << ',' << link.index << ',' << int(link.relation);
        }
    }
    return out.str();
}
}
FK_TEST(BlendCavityCancellationPreservesBodyAndClearsOutputHistory) {
    const Body body = cavityFixture();
    const auto before = writeBodyBinary(body);
    const auto contour = blendCavityContour(body, EdgeId(10));
    BodyHistory history = sameIndexHistory(body, body);
    // history is an OUTPUT parameter, explicitly reset at public entry.
    const auto expectedHistory = cavityHistorySnapshot(BodyHistory{});
    const auto oldCancellation = currentCancellation;
    bool cancelled = false, published = false;
    {
        auto token = std::make_shared<std::atomic<bool>>(true);
        CancellationScope scope(token);
        try {
            const Body result = blendEdges(body, contour, .5, false, &history, false);
            (void)result;
            published = true;
        } catch (const OperationCancelled &) {
            cancelled = true;
        }
    }
    FK_CHECK(cancelled && !published);
    FK_CHECK(currentCancellation == oldCancellation);
    FK_CHECK(writeBodyBinary(body) == before);
    FK_CHECK(cavityHistorySnapshot(history) == expectedHistory);
}
FK_TEST(BlendCavityConsumedSideRejectsWithoutBodyOrPartialHistory) {
    const Body body = cavityFixture();
    const auto before = writeBodyBinary(body);
    const auto contour = blendCavityContour(body, EdgeId(10));
    BodyHistory history = sameIndexHistory(body, body);
    // history is an OUTPUT parameter, explicitly reset at public entry.
    const auto expectedHistory = cavityHistorySnapshot(BodyHistory{});
    bool consumed = false;
    try {
        const Body result = blendEdges(body, contour, 2.1, false, &history, false);
        (void)result;
    } catch (const std::domain_error &error) {
        consumed = std::string(error.what()).find("raggio troppo grande: il raccordo consuma o inverte la faccia") != std::string::npos;
    }
    FK_CHECK(consumed);
    FK_CHECK(writeBodyBinary(body) == before);
    FK_CHECK(cavityHistorySnapshot(history) == expectedHistory);
}
