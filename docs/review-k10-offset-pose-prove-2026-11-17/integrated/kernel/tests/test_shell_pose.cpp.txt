#include "fk_body_check.h"
#include "fk_body_io.h"
#include "fk_classify.h"
#include "fk_surface_algo.h"
#include "fk_history.h"
#include "fk_mass.h"
#include "fk_shell.h"
#include "fk_test.h"
#include "fk_transform.h"
#include <cmath>
#include <fstream>
#include <iterator>
#include <string>

using namespace ForgeCad::Kernel;
namespace {
void shellPose(double scale, bool rotated) {
    std::ifstream input(std::string(FORGEKERNEL_TEST_DATA_DIR) + "/shell_stitched_tangent.body", std::ios::binary);
    FK_CHECK(bool(input));
    if (!input) return;
    const std::string bytes(std::istreambuf_iterator<char>(input), {});
    const Body original = readBodyBinary(bytes);
    const std::string originalBefore = writeBodyBinary(original);
    Body source = scale == 1 ? original : scaleBody(original, Vec3(), scale);
    if (rotated)
        source = transformBody(source, Transform3::rotation(Vec3(12, -23, 7), Vec3(1, 2, .3), .42));
    const std::string before = writeBodyBinary(source);
    CheckOptions checks;
    checks.loopCrossings = true;
    FK_CHECK(checkBody(source, checks).empty());
    const double outerVolume = massProperties(source).volume;
    // Source and thickness undergo the same similarity. The previous public
    // shell test retains the original untransformed .5/1 mm and open cases.
    BodyHistory history = makeHistory({&source}, source);
    Body result;
    try {
        result = shellBody(source, {}, .5 * scale, &history);
    } catch (const std::exception &error) {
        FK_CHECK(writeBodyBinary(source) == before);
        FK_CHECK(writeBodyBinary(original) == originalBefore);
        FK_CHECK(history.operandCount() == 0);
        fktest::reportFailure(__FILE__, __LINE__, error.what());
        return;
    }
    FK_CHECK(writeBodyBinary(source) == before);
    FK_CHECK(writeBodyBinary(original) == originalBefore);
    FK_CHECK(history.operandCount() == 1);
    FK_CHECK(!result.isSheet() && result.shells().size() == 2);
    for (EdgeId edge : result.edges()) FK_CHECK(!result.isLaminar(edge));
    // checkBody and mass integration are geometric regression checks, not a
    // continuous global certificate for the generated freeform shell.
    FK_CHECK(checkBody(result, checks).empty());
    int measuredInnerFaces = 0;
    for (FaceId face : result.faces()) {
        FK_CHECK(history.isDeclared(EntityKind::Face, face.index));
        const auto &origins = history.origins(EntityKind::Face, face.index);
        FK_CHECK(origins.size() == 1);
        for (const auto &origin : origins) {
            FK_CHECK(origin.operand == 0 && origin.kind == EntityKind::Face);
            FK_CHECK(source.contains(FaceId{origin.index}));
        }
        if (origins.size() != 1 || origins.front().operand != 0 || origins.front().kind != EntityKind::Face
            || !source.contains(FaceId{origins.front().index})) continue;
        const auto &parent = source.face(FaceId{origins.front().index});
        if (result.face(face).surface == parent.surface) continue;
        Vec3 point;
        Vec2 uv;
        const bool inside = faceInteriorPoint(result, face, point, &uv);
        FK_CHECK(inside);
        if (!inside) continue;
        if (parent.surface->type() == SurfaceType::Plane) {
            const auto foot = projectPoint(*parent.surface, point);
            uv = Vec2(foot.u, foot.v);
        }
        const Vec3 normal = parent.surface->normal(uv.x(), uv.y()) * (parent.sense ? 1. : -1.);
        // One interior-point regression per inner face, using the same 2e-5 mm
        // budget as ShellStitchedTolerantTangencies; not an interval proof.
        FK_CHECK(norm(point - parent.surface->point(uv.x(), uv.y()) + (.5 * scale) * normal) <= 2e-5);
        ++measuredInnerFaces;
    }
    FK_CHECK(measuredInnerFaces >= 6);
    const auto mass = massProperties(result);
    FK_CHECK(std::isfinite(mass.volume) && mass.volume > 0 && mass.volume < outerVolume);
    // Serialization must retain the completed B-rep and its tolerance metadata.
    const std::string encoded = writeBodyBinary(result);
    const Body decoded = readBodyBinary(encoded);
    FK_CHECK(writeBodyBinary(decoded) == encoded);
    FK_CHECK(checkBody(decoded, checks).empty());
}
}

FK_TEST(ShellStitchedScaledSimilarity) { shellPose(2.5, false); }
FK_TEST(ShellStitchedScaledAndRotatedSimilarity) { shellPose(2.5, true); }
FK_TEST(ShellStitchedRotationControl) { shellPose(1, true); }
