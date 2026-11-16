#include "fk_body_io.h"
#include "fk_bspline.h"
#include "fk_cancel.h"
#include "fk_curve_projection.h"
#include "fk_fill.h"
#include "fk_test.h"
#include <limits>
using namespace ForgeCad::Kernel;
namespace {
void checkProjection(const CurvePtr<3> &source, const Interval &range, const Frame3 &frame) {
    const auto result = projectCurveAffine(source, range, frame);
    FK_CHECK(bool(result));
    if (!result)
        return;
    for (int i = -1; i <= 101; ++i) {
        // Include extrapolation beyond the requested range and both jets at knots.
        const double t = range.lo + range.length() * i / 100.;
        for (bool left : {false, true}) {
            Vec3 original[3];
            Vec2 projected[3];
            if (left) {
                source->evaluateLeft(t, 2, original);
                result->evaluateLeft(t, 2, projected);
            } else {
                source->evaluate(t, 2, original);
                result->evaluate(t, 2, projected);
            }
            for (int k = 0; k <= 2; ++k) {
                const Vec3 q = k == 0 ? frame.toLocal(original[k])
                                      : Vec3(dot(original[k], frame.xDir()), dot(original[k], frame.yDir()),
                                             dot(original[k], frame.zDir()));
                FK_CHECK(distance(projected[k], Vec2(q.x(), q.y())) <=
                         1e-10 * std::max(1., norm(original[k])));
            }
        }
    }
}
CurvePtr<3> rationalSource() {
    return std::make_shared<BSplineCurve<3>>(
        3, std::vector<double>{0, 0, 0, 0, .4, .4, .4, 1, 1, 1, 1},
        std::vector<Vec3>{
            {0, 0, 1}, {.1, .2, 2}, {.3, .1, 1}, {.4, .3, 3}, {.6, -.2, 1}, {.8, .5, 2}, {1, 0, 1}},
        std::vector<double>{1, .7, 1.2, 1, .8, 1.1, 1});
}
} // namespace
FK_TEST(CurveProjectionRationalKnotsWeightsAndJets) {
    const auto source = rationalSource();
    const Frame3 frame(Vec3(1, 2, 3), Vec3(.2, -.3, 1), Vec3(1, 0, 0));
    checkProjection(source, {.1, .9}, frame);
    const auto projected =
        std::dynamic_pointer_cast<const BSplineCurve<2>>(projectCurveAffine(source, {.1, .9}, frame));
    const auto spline = std::dynamic_pointer_cast<const BSplineCurve<3>>(source);
    FK_CHECK(projected && projected->degree() == spline->degree() && projected->knots() == spline->knots() &&
             projected->weights() == spline->weights());
    for (bool left : {false, true}) {
        Vec2 a[3];
        Vec3 b[3];
        if (left) {
            projected->evaluateLeft(.4, 2, a);
            source->evaluateLeft(.4, 2, b);
        } else {
            projected->evaluate(.4, 2, a);
            source->evaluate(.4, 2, b);
        }
        FK_CHECK(distance(a[1], Vec2(dot(b[1], frame.xDir()), dot(b[1], frame.yDir()))) < 1e-10);
    }
}
FK_TEST(CurveProjectionAffineShearNestedWrappers) {
    const double matrix[3][3] = {{2, .4, .2}, {-.3, .8, .5}, {.2, .1, 1.3}};
    CurvePtr<3> source = std::make_shared<TrimmedCurve<3>>(rationalSource(), .03, .97);
    source = std::make_shared<TransformedCurve>(source, Transform3::fromParts(matrix, Vec3(2, -1, .5)));
    source = std::make_shared<TrimmedCurve<3>>(source, .07, .93);
    source = std::make_shared<TransformedCurve>(source, Transform3::translation(Vec3(-1, 3, 2)) *
                                                            Transform3::rotation(Vec3(), Vec3(1, 2, 3), .7));
    const Frame3 frame(Vec3(-2, 1, .3), Vec3(.4, .2, 1), Vec3(1, 0, 0));
    checkProjection(source, {.1, .9}, frame);
    const auto projected = projectCurveAffine(source, {.1, .9}, frame);
    FK_CHECK(projected->domain().lo == source->domain().lo && projected->domain().hi == source->domain().hi);
}
FK_TEST(CurveProjectionLinesDegenerateAndUnsupportedConics) {
    const Frame3 frame;
    const Interval range{-3, 7};
    for (Vec3 direction : {Vec3(2, 1, 3), Vec3(0, 0, 1)})
        checkProjection(std::make_shared<Line<3>>(Vec3(1, 2, 3), direction), range, frame);
    CurvePtr<3> trimmedLine =
        std::make_shared<TrimmedCurve<3>>(std::make_shared<Line<3>>(Vec3(1, 2, 3), Vec3(2, 1, 3)), 0, 10);
    checkProjection(trimmedLine, {2, 4}, frame);
    trimmedLine = std::make_shared<TrimmedCurve<3>>(trimmedLine, 2, 4);
    checkProjection(trimmedLine, {2.3, 3.7}, frame);
    const auto circle = std::make_shared<Circle<3>>(Vec3(), Vec3(1, 0, 0), Vec3(0, 1, 0), 2);
    const auto ellipse = std::make_shared<Ellipse<3>>(Vec3(), Vec3(1, 0, 0), Vec3(0, 1, 0), 2, 1);
    FK_CHECK(!projectCurveAffine(circle, {0, 1}, frame));
    FK_CHECK(!projectCurveAffine(ellipse, {0, 1}, frame));
    FK_CHECK(!projectCurveAffine(
        std::make_shared<TransformedCurve>(circle, Transform3::translation(Vec3(2, 3, 4))), {0, 1}, frame));
}
FK_TEST(CurveProjectionInvalidAndCancellation) {
    const auto source = rationalSource();
    const Frame3 frame;
    FK_CHECK_THROWS(projectCurveAffine({}, {0, 1}, frame));
    for (Interval range : std::vector<Interval>{{0, 0}, {1, 0}, {0, std::numeric_limits<double>::infinity()}})
        FK_CHECK_THROWS(projectCurveAffine(source, range, frame));
    auto token = std::make_shared<std::atomic<bool>>(true);
    CancellationScope scope(token);
    bool cancelled = false;
    try {
        projectCurveAffine(source, {0, 1}, frame);
    } catch (const OperationCancelled &) {
        cancelled = true;
    }
    FK_CHECK(cancelled);
}
FK_TEST(CurveProjectionFillRationalAndBodyRoundtrip) {
    const auto side = std::make_shared<BSplineCurve<3>>(
        3, std::vector<double>{0, 0, 0, 0, 1, 1, 1, 1},
        std::vector<Vec3>{{0, 0, 0}, {.3, 0, 0}, {1.5, 0, 0}, {2, 0, 0}}, std::vector<double>{1, .7, 1.2, 1});
    const auto line = [](Vec3 a, Vec3 b) {
        return PathSegment{std::make_shared<Line<3>>(a, b - a), {0, distance(a, b)}};
    };
    const std::vector<PathSegment> boundary{{side, {0, 1}},
                                            line(Vec3(2, 0, 0), Vec3(2, 1, 0)),
                                            line(Vec3(2, 1, 0), Vec3(0, 1, 0)),
                                            line(Vec3(0, 1, 0), Vec3(0, 0, 0))};
    const Body filled = fillSurface(boundary, {}, {});
    FK_CHECK(filled.faces().size() == 1);
    const auto verify = [&](const Body &body) {
        bool found = false;
        for (auto face : body.faces())
            for (auto loop : body.face(face).loops)
                for (auto use : body.loopFins(loop)) {
                    const auto &fin = body.fin(use);
                    const auto &edge = body.edge(fin.edge);
                    if (edge.curve->type() != CurveType::BSpline)
                        continue;
                    const auto uv = std::dynamic_pointer_cast<const BSplineCurve<2>>(fin.pcurve);
                    FK_CHECK(bool(uv));
                    if (!uv)
                        continue;
                    FK_CHECK(uv->degree() == 3 && uv->knots() == side->knots() &&
                             uv->weights() == side->weights());
                    found = true;
                    for (int i = 0; i <= 100; ++i) {
                        double t = i / 100.;
                        const auto q = uv->point(t);
                        FK_CHECK(distance(body.face(face).surface->point(q.x(), q.y()),
                                          edge.curve->point(t)) < 1e-7);
                    }
                }
        FK_CHECK(found);
    };
    verify(filled);
    verify(readBodyBinary(writeBodyBinary(filled)));
}
