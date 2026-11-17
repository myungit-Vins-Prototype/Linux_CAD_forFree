#include "fk_mapping_fit.h"
#include "fk_bspline_surface.h"
#include "fk_cancel.h"
#include "fk_test.h"
#include <cmath>
#include <functional>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace ForgeCad::Kernel;
namespace {
using Value = std::function<Vec3(double, double)>;
constexpr double tolerance = 1e-8;
Vec3 planarValue(double u, double v) { return {u, v, 2 * u - v + .5}; }
detail::MappingJet planarJet(double u, double v, int, int) {
    return {planarValue(u, v), {1, 0, 2}, {0, 1, -1}, {0, 0, 0}};
}
void expectRejected(const Value &value, const detail::MappingJetFunction &jet = {}, int degree = 3,
    const std::vector<double> &us = {0, 1}, const std::vector<double> &vs = {0, 1}, double budget = tolerance) {
    // A negative initial accumulator is already accepted by successful fits.
    // Neither it nor a return value may be published before complete success.
    double deviation = -17;
    SurfacePtr result;
    bool rejected = false;
    try { result = detail::fitMappingSurface(us, vs, value, jet, budget, deviation, .001, degree); }
    catch (const std::domain_error &) { rejected = true; }
    FK_CHECK(rejected);
    FK_CHECK(!result);
    FK_CHECK(deviation == -17);
}
}

FK_TEST(MappingFitFiniteCubicQuinticGridsAndDeviation) {
    for (int degree : {3, 5}) for (bool analytic : {false, true}) {
        const std::vector<double> us{-2, -.5, 1}, vs{3, 3.5, 5};
        const auto beforeU = us, beforeV = vs;
        const detail::MappingJetFunction jet = analytic ? detail::MappingJetFunction(planarJet) : detail::MappingJetFunction();
        double deviation = -1;
        const auto result = detail::fitMappingSurface(us, vs, planarValue, jet, tolerance, deviation, .001, degree);
        FK_CHECK(result && std::isfinite(deviation) && deviation >= 0 && deviation <= tolerance);
        const auto spline = std::dynamic_pointer_cast<const BSplineSurface>(result);
        FK_CHECK(spline && spline->uDegree() == degree && spline->vDegree() == degree);
        FK_CHECK(result->uDomain().lo == us.front() && result->uDomain().hi == us.back());
        FK_CHECK(result->vDomain().lo == vs.front() && result->vDomain().hi == vs.back());
        // Point regressions on the emitted surface, not a continuous proof.
        for (double u : {-2., -.25, 1.}) for (double v : {3., 4.25, 5.})
            FK_CHECK(distance(result->point(u, v), planarValue(u, v)) <= tolerance);
        FK_CHECK(us == beforeU && vs == beforeV);
        deviation = .25;
        const auto again = detail::fitMappingSurface({0, 1}, {0, 1}, planarValue, jet, tolerance, deviation, .001, degree);
        FK_CHECK(again && deviation == .25);
    }
}

FK_TEST(MappingFitNonFiniteInteriorProbeCannotPublishSurface) {
    const double nan = std::numeric_limits<double>::quiet_NaN(), inf = std::numeric_limits<double>::infinity();
    for (int degree : {3, 5}) for (bool analytic : {false, true}) for (double bad : {nan, inf, -inf}) {
        const double probe = degree == 3 ? .125 : .1;
        bool reachedProbe = false;
        const Value value = [&](double u, double v) {
            auto point = planarValue(u, v);
            if (u == probe && v == probe) { reachedProbe = true; point[2] = bad; }
            return point;
        };
        // Both actual production callers currently pass an empty analytic jet.
        // A finite analytic jet additionally isolates acceptance-probe reads.
        const detail::MappingJetFunction jet = analytic ? detail::MappingJetFunction(planarJet) : detail::MappingJetFunction();
        expectRejected(value, jet, degree);
        FK_CHECK(reachedProbe);
    }
}

FK_TEST(MappingFitNonFiniteCornerAndStencilValuesReject) {
    const double nan = std::numeric_limits<double>::quiet_NaN(), inf = std::numeric_limits<double>::infinity();
    for (int degree : {3, 5}) for (double location : {0., .001}) for (double bad : {nan, inf}) {
        bool reachedValue = false;
        const Value value = [&](double u, double v) {
            auto point = planarValue(u, v);
            if (u == location && v == 0) { reachedValue = true; point[0] = bad; }
            return point;
        };
        expectRejected(value, {}, degree);
        FK_CHECK(reachedValue);
    }
}

FK_TEST(MappingFitEveryAnalyticJetVectorMustBeFinite) {
    const double nan = std::numeric_limits<double>::quiet_NaN(), inf = std::numeric_limits<double>::infinity();
    for (int degree : {3, 5}) for (int field = 0; field < 4; ++field) for (double bad : {nan, inf}) {
        bool reachedJet = false;
        const detail::MappingJetFunction jet = [&](double u, double v, int su, int sv) {
            reachedJet = true;
            auto data = planarJet(u, v, su, sv);
            Vec3 *vectors[]{&data.p, &data.pu, &data.pv, &data.puv};
            (*vectors[field])[field % 3] = bad;
            return data;
        };
        expectRejected(planarValue, jet, degree);
        FK_CHECK(reachedJet);
    }
}

FK_TEST(MappingFitToleranceAndGridPreflightBeforeCallbacks) {
    const double nan = std::numeric_limits<double>::quiet_NaN(), inf = std::numeric_limits<double>::infinity();
    int callbacks = 0;
    const Value value = [&](double u, double v) { ++callbacks; return planarValue(u, v); };
    const detail::MappingJetFunction jet = [&](double u, double v, int su, int sv) {
        ++callbacks; return planarJet(u, v, su, sv);
    };
    for (double budget : {0., -1., nan, inf}) {
        callbacks = 0;
        expectRejected(value, jet, 3, {0, 1}, {0, 1}, budget);
        FK_CHECK(callbacks == 0);
    }
    const double largest = std::numeric_limits<double>::max();
    const std::vector<std::vector<double>> grids{{}, {0}, {0, 0, 1}, {1, 0}, {0, 1, .5},
        {0, nan, 1}, {0, inf}, {-largest, largest}};
    for (const auto &grid : grids) {
        callbacks = 0;
        expectRejected(value, jet, 3, grid, {0, 1});
        FK_CHECK(callbacks == 0);
        callbacks = 0;
        expectRejected(value, jet, 3, {0, 1}, grid);
        FK_CHECK(callbacks == 0);
    }
}

FK_TEST(MappingFitCallbackErrorsAndCancellationDoNotPublish) {
    double deviation = -17;
    SurfacePtr result;
    bool propagated = false;
    try {
        result = detail::fitMappingSurface({0, 1}, {0, 1}, [](double, double) -> Vec3 {
            throw std::runtime_error("mapping callback sentinel");
        }, {}, tolerance, deviation);
    } catch (const std::runtime_error &error) { propagated = std::string(error.what()) == "mapping callback sentinel"; }
    FK_CHECK(propagated && !result && deviation == -17);
    const auto token = std::make_shared<std::atomic<bool>>(true); CancellationScope scope(token);
    bool cancelled = false, called = false;
    try {
        result = detail::fitMappingSurface({0, 1}, {0, 1}, [&](double u, double v) {
            called = true; return planarValue(u, v);
        }, {}, tolerance, deviation);
    } catch (const OperationCancelled &) { cancelled = true; }
    FK_CHECK(cancelled && !called && !result && deviation == -17);
}
