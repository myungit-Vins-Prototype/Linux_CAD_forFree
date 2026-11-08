#include "fk_test.h"
#include "fk_body_check.h"
#include "fk_boolean.h"
#include "fk_marching.h"
#include "fk_pcurve.h"
#include "fk_primitives.h"

#include <limits>
#include "fk_parallel.h"
#include "fk_offset.h"
#include "fk_bspline_surface.h"
#include <thread>
#include <atomic>

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

FK_TEST(ToleranceDocumentScopesAndWorkers) {
    FK_CHECK(linearResolution() == kLinearResolution);
    Body perturbed = makeBox(Frame3{}, 2, 3, 4);
    for (auto v : perturbed.vertices()) perturbed.vertex(v).tolerance = 0;
    for (auto e : perturbed.edges()) perturbed.edge(e).tolerance = 0;
    perturbed.vertex(perturbed.vertices().front()).point += Vec3(3e-8, 0, 0);
    FK_CHECK(!hasIssue(perturbed, CheckCode::VertexOffEdge));
    {
        PrecisionScope fine(1e-9);
        FK_CHECK(hasIssue(perturbed, CheckCode::VertexOffEdge));
        FK_CHECK(std::abs(BooleanOptions{}.tolerance - 1e-8) < 1e-23);
        FK_CHECK(std::abs(SurfaceIntersectionOptions{}.fitTolerance - 1e-11) < 1e-25);
        std::atomic<bool> correct{true};
        parallelFor(64, 4, [&](std::size_t) {
            if (linearResolution() != 1e-9) correct = false;
            parallelFor(3, 3, [&](std::size_t) { if (linearResolution() != 1e-9) correct = false; });
        });
        FK_CHECK(correct);
        std::thread other([&] { if (linearResolution() != kLinearResolution) correct = false; });
        other.join(); FK_CHECK(correct);
        try { PrecisionScope nested(1e-8); throw std::runtime_error("unwind"); } catch (const std::runtime_error &) {}
        FK_CHECK(linearResolution() == 1e-9);
    }
    FK_CHECK(linearResolution() == kLinearResolution);
    for (double invalid : {0., -1., 1e-11, 1e-6, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()}) {
        bool rejected = false;
        try { PrecisionScope scope(invalid); } catch (const std::invalid_argument &) { rejected = true; }
        FK_CHECK(rejected && linearResolution() == kLinearResolution);
    }
}

FK_TEST(ToleranceDocumentOffsetAccuracy) {
    std::vector<Vec3> poles;
    std::vector<double> weights;
    for (int i=0;i<4;++i) for (int j=0;j<4;++j) {
        poles.emplace_back(i, j, .1*i*j);
        weights.push_back(1 + .1*((i+j)%3));
    }
    BSplineSurface source(3,3,{0,0,0,0,1,1,1,1},{0,0,0,0,1,1,1,1},4,4,poles,weights);
    double measured[2]{};
    for (int profile=0;profile<2;++profile) {
        const double accuracy=profile ? 1e-9 : kLinearResolution;
        PrecisionScope scope(accuracy);
        const auto offset=offsetSurface(source,.2,{0,1},{0,1});
        for (int i=0;i<=37;++i) for (int j=0;j<=37;++j) {
            const double u=i/37.,v=j/37.;
            const Vec3 expected=source.point(u,v)+.2*source.normal(u,v);
            measured[profile]=std::max(measured[profile],distance(offset->point(u,v),expected));
        }
        FK_CHECK(measured[profile] < 2*accuracy);
    }
    FK_CHECK(measured[1] < measured[0]/10);
}
