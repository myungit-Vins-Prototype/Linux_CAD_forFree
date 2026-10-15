#include "fk_test.h"
#include "fk_body_check.h"
#include "fk_boolean.h"
#include "fk_marching.h"
#include "fk_pcurve.h"
#include "fk_primitives.h"

#include <limits>

using namespace ForgeCad::Kernel;

namespace {
bool hasIssue(const Body &body, CheckCode code, bool geometry = true) {
    CheckOptions options;
    options.geometry = geometry;
    for (const auto &issue : checkBody(body, options))
        if (issue.code == code) return true;
    return false;
}
}

FK_TEST(ToleranceDefaults) {
    FK_CHECK(BooleanOptions{}.tolerance == kBooleanConnectionTolerance);
    FK_CHECK(SurfaceIntersectionOptions{}.tolerance == kMarchingConnectionTolerance);
    FK_CHECK(SurfaceIntersectionOptions{}.fitTolerance == kMarchingFitTolerance);
    FK_CHECK(kPCurveTolerance == kLinearResolution);
    FK_CHECK(checkBody(makeBox(Frame3{}, 2, 3, 4)).empty());
}

FK_TEST(ToleranceInvalidMetadata) {
    for (double value : {-1.0, std::numeric_limits<double>::infinity(),
                         -std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        for (int kind = 0; kind < 3; ++kind) {
            Body body = makeBox(Frame3{}, 2, 3, 4);
            if (kind == 0) body.vertex(body.vertices().front()).tolerance = value;
            if (kind == 1) body.edge(body.edges().front()).tolerance = value;
            if (kind == 2) body.fin(body.fins().front()).pcurveTolerance = value;
            FK_CHECK(hasIssue(body, CheckCode::InvalidTolerance));
            FK_CHECK(hasIssue(body, CheckCode::InvalidTolerance, false));
        }
    }
    for (double value : {0.0, 1e-12, 9e-4}) {
        Body body = makeBox(Frame3{}, 2, 3, 4);
        for (auto v : body.vertices()) body.vertex(v).tolerance = value;
        for (auto e : body.edges()) body.edge(e).tolerance = value;
        for (auto f : body.fins()) body.fin(f).pcurveTolerance = value;
        FK_CHECK(checkBody(body).empty());
    }
}

FK_TEST(ToleranceVertexBudget) {
    for (double scale : {1.0, 1000.0}) {
        for (double gap : {0.5 * kLinearResolution, 2 * kLinearResolution}) {
            Body body = makeBox(Frame3{}, 2 * scale, 3 * scale, 4 * scale);
            const auto v = body.vertices().front();
            body.vertex(v).point += Vec3(gap, 0, 0);
            FK_CHECK(hasIssue(body, CheckCode::VertexOffEdge) == (gap > kLinearResolution));
            body.vertex(v).tolerance = 3 * kLinearResolution;
            FK_CHECK(!hasIssue(body, CheckCode::VertexOffEdge));
            body.vertex(v).tolerance = 0;
            for (auto e : body.edges()) body.edge(e).tolerance = 3 * kLinearResolution;
            FK_CHECK(!hasIssue(body, CheckCode::VertexOffEdge));
        }
    }
}

FK_TEST(TolerancePCurveBudget) {
    Body body = makeBox(Frame3{}, 2, 3, 4);
    const auto f = body.fins().front();
    auto &fin = body.fin(f);
    const auto *line = dynamic_cast<const Line<2> *>(fin.pcurve.get());
    FK_CHECK(line != nullptr);
    if (!line) return;
    // Sul piano del box le coordinate UV sono distanze fisiche.
    fin.pcurve = std::make_shared<Line<2>>(line->origin() + Vec2(3e-6, 0), line->direction());
    FK_CHECK(hasIssue(body, CheckCode::PCurveOffEdge));
    body.edge(fin.edge).tolerance = 2e-6;
    fin.pcurveTolerance = 2e-6;
    FK_CHECK(!hasIssue(body, CheckCode::PCurveOffEdge)); // somma, non massimo
    fin.pcurveTolerance = 0;
    FK_CHECK(hasIssue(body, CheckCode::PCurveOffEdge));
}

FK_TEST(ToleranceEdgeFaceBudget) {
    for (double scale : {1.0, 1000.0}) {
        Body body = makeBox(Frame3{}, 2 * scale, 3 * scale, 4 * scale);
        auto &edge = body.edge(body.edges().front());
        const auto *line = dynamic_cast<const Line<3> *>(edge.curve.get());
        FK_CHECK(line != nullptr);
        if (!line) continue;
        edge.curve = std::make_shared<Line<3>>(line->origin() + Vec3(2e-6, 2e-6, 2e-6), line->direction());
        FK_CHECK(hasIssue(body, CheckCode::EdgeOffFace));
        edge.tolerance = 4e-6;
        FK_CHECK(checkBody(body).empty());
    }
}
