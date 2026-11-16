#include "fk_body_check.h"
#include "fk_body_io.h"
#include "fk_bspline.h"
#include "fk_cancel.h"
#include "fk_curve_algo.h"
#include "fk_curve_ops.h"
#include "fk_history.h"
#include "fk_mass.h"
#include "fk_surface_algo.h"
#include "fk_test.h"
#include "fk_thicken.h"
#include <algorithm>
#include <limits>
#include <sstream>
using namespace ForgeCad::Kernel;
namespace {
const std::vector<double> z27 = {0,
                                 0,
                                 0,
                                 0.0023926594471666389,
                                 -0.03158506535957388,
                                 0.22274247758415186,
                                 -1.0588644597919092,
                                 3.7068614333740251,
                                 -10.150702528319338,
                                 22.626599248232061,
                                 -42.105538437516088,
                                 66.431608164324672,
                                 -89.673893632054558,
                                 104.07526656293437,
                                 -104.07526656293437,
                                 89.673893632054558,
                                 -66.431608164324672,
                                 42.105538437516088,
                                 -22.626599248232061,
                                 10.150702528319338,
                                 -3.7068614333740251,
                                 1.0588644597919092,
                                 -0.22274247758415186,
                                 0.03158506535957388,
                                 -0.0023926594471666389,
                                 0,
                                 0,
                                 0};

CurvePtr<2> straight(Vec2 a, Vec2 b) {
    return std::make_shared<BSplineCurve<2>>(1, std::vector<double>{0, 0, 1, 1}, std::vector<Vec2>{a, b});
}
// A single degree-27 span whose visible lobes fall between the legacy fit
// samples. The rational homogeneous numerator preserves the same hidden
// zeros, with positive nonconstant weights and X(t) equal to affine t.
Body fixture(double scale, bool rational, const Frame3 &frame, bool sense, bool trimmed = false,
             bool reverse = false, bool hole = false) {
    std::vector<Vec2> poles;
    std::vector<double> weights;
    for (int i = 0; i < 28; ++i) {
        const double x = i / 27., w = rational ? 1 + .5 * x : 1;
        const double numerator = x + (rational ? .5 * i * (i - 1.) / (27 * 26.) : 0);
        poles.emplace_back(scale * numerator / w, scale * z27[i] / w);
        if (rational)
            weights.push_back(w);
    }
    const Interval natural = trimmed ? Interval{2, 5} : Interval{0, 1};
    std::vector<double> knots(28, natural.lo);
    knots.insert(knots.end(), 28, natural.hi);
    CurvePtr<2> bottom = std::make_shared<BSplineCurve<2>>(27, knots, poles, weights);
    Interval range = natural;
    if (trimmed) {
        bottom = std::make_shared<TrimmedCurve<2>>(bottom, 2, 5);
        bottom = std::make_shared<TrimmedCurve<2>>(bottom, 2.3, 4.7);
        range = {2.3, 4.7};
    }
    const auto a = bottom->point(range.lo), b = bottom->point(range.hi);
    const std::vector<Vec2> corners{a, b, {b.x(), scale}, {a.x(), scale}};
    std::vector<Vec3> vertices;
    for (auto q : corners)
        vertices.push_back(frame.toGlobal({q.x(), q.y(), 0}));
    std::vector<Body::BuildEdge> edges;
    Body::BuildFace face;
    face.surface = std::make_shared<Plane>(frame);
    face.sense = sense;
    std::vector<Body::BuildFin> outer;
    if (reverse) {
        bottom = reversedCurve(bottom);
        range = {-range.hi, -range.lo};
    }
    edges.push_back({reverse ? 1 : 0, reverse ? 0 : 1, embedCurve(bottom, frame), range, 1e-9});
    outer.push_back({0, !reverse, bottom, 1e-9});
    for (int i = 1; i < 4; ++i) {
        auto uv = straight(corners[i], corners[(i + 1) % 4]);
        edges.push_back({i, (i + 1) % 4, embedCurve(uv, frame), {0, 1}, 1e-9});
        outer.push_back({i, true, uv, 1e-9});
    }
    face.loops = {outer};
    if (hole) {
        const std::vector<Vec2> points{{.35 * scale, .35 * scale},
                                       {.35 * scale, .65 * scale},
                                       {.65 * scale, .65 * scale},
                                       {.65 * scale, .35 * scale}};
        std::vector<Body::BuildFin> inner;
        for (auto q : points)
            vertices.push_back(frame.toGlobal({q.x(), q.y(), 0}));
        for (int i = 0; i < 4; ++i) {
            auto uv = straight(points[i], points[(i + 1) % 4]);
            inner.push_back({int(edges.size()), true, uv, 1e-9});
            edges.push_back({4 + i, 4 + (i + 1) % 4, embedCurve(uv, frame), {0, 1}, 1e-9});
        }
        face.loops.push_back(inner);
    }
    if (!sense) {
        for (auto &loop : face.loops) {
            std::reverse(loop.begin(), loop.end());
            for (auto &use : loop)
                use.sense = !use.sense;
        }
    }
    return Body::buildSheet(vertices, edges, {face});
}

CurvePtr<3> basis3(CurvePtr<3> curve) {
    for (;;) {
        if (curve->type() == CurveType::Trimmed)
            curve = static_cast<const TrimmedCurve<3> &>(*curve).basis();
        else if (curve->type() == CurveType::Transformed)
            curve = static_cast<const TransformedCurve &>(*curve).basis();
        else
            return curve;
    }
}
void verify(const Body &source, ThickenOptions options, bool roundtrip = false) {
    FK_CHECK(source.isSheet() && source.faces().size() == 1);
    FK_CHECK(checkBody(source, {true, true}).empty());
    const auto before = writeBodyBinary(source);
    const auto &face = source.face(source.faces().front());
    const auto &plane = static_cast<const Plane &>(*face.surface).frame();
    const Vec3 normal = (face.sense ? 1 : -1) * plane.zDir();
    if (options.useDirection)
        options.direction = normal;
    double lower = 0, upper = options.thickness;
    if (options.side == ThickenSide::Backward) {
        lower = -options.thickness;
        upper = 0;
    }
    if (options.side == ThickenSide::Both) {
        lower = -.5 * options.thickness;
        upper = .5 * options.thickness;
    }
    BodyHistory history;
    const auto result = thickenSheet(source, {}, options, &history);
    FK_CHECK(history.operandCount() == 1);
    const auto &original = source.edge(source.edges().front());
    const auto base = basis3(original.curve);
    const auto check = [&](const Body &body) {
        FK_CHECK(!body.isSheet() && checkBody(body, {true, true}).empty());
        const double volume = massProperties(body).volume;
        const double expected = faceArea(source, source.faces().front()) * options.thickness;
        FK_CHECK(volume > 0);
        FK_CHECK_NEAR(volume, expected, 1e-8 * std::max(1., expected));
        std::vector<FaceId> walls;
        for (auto id : body.faces()) {
            FK_CHECK(history.isDeclared(EntityKind::Face, id.index));
            for (auto origin : history.origins(EntityKind::Face, id.index))
                if (origin.kind == EntityKind::Edge && origin.index == source.edges().front().index) {
                    walls.push_back(id);
                    break;
                }
        }
        FK_CHECK(!walls.empty());
        if (walls.empty())
            throw std::runtime_error("missing generated wall history");
        double error = 0, coherence = 0;
        for (auto id : walls) {
            const auto &wallFace = body.face(id);
            const auto *surface = dynamic_cast<const ExtrusionSurface *>(wallFace.surface.get());
            FK_CHECK(surface != nullptr);
            if (!surface)
                throw std::runtime_error("known planar wall not original-generator extrusion");
            const auto emitted = basis3(surface->curve());
            if (base->type() == CurveType::BSpline) {
                const auto &input = static_cast<const BSplineCurve<3> &>(*base);
                const auto *output = dynamic_cast<const BSplineCurve<3> *>(emitted.get());
                FK_CHECK(output != nullptr);
                if (!output)
                    throw std::runtime_error("wall generator family changed");
                FK_CHECK(output->degree() == input.degree() && output->knots() == input.knots());
                FK_CHECK(output->poleCount() == input.poleCount());
                if (output->poleCount() == input.poleCount())
                    for (int i = 0; i < input.poleCount(); ++i)
                        FK_CHECK(output->weight(i) == input.weight(i));
            }
            const double middle = .5 * (lower + upper);
            for (int i = 0; i <= 2000; ++i) {
                const double t = original.range.lo + original.range.length() * i / 2000.;
                const auto exact = original.curve->point(t) + middle * normal;
                error = std::max(error, distance(surface->point(t, .5 * options.thickness), exact));
            }
            for (auto loop : wallFace.loops)
                for (auto use : body.loopFins(loop)) {
                    const auto &fin = body.fin(use);
                    const auto &edge = body.edge(fin.edge);
                    FK_CHECK(bool(fin.pcurve));
                    if (!fin.pcurve)
                        throw std::runtime_error("missing actual wall pcurve");
                    for (int i = 0; i <= 2000; ++i) {
                        const double t = edge.range.lo + edge.range.length() * i / 2000.;
                        const auto uv = fin.pcurve->point(t);
                        coherence = std::max(coherence,
                                             distance(edge.curve->point(t), surface->point(uv.x(), uv.y())));
                    }
                }
        }
        FK_CHECK(error <= 1e-9 && coherence <= 1e-9);
        // Independent nearest-support check at the strongest hidden lobe.
        double height = 0, peak = original.range.lo;
        for (int i = 0; i <= 10000; ++i) {
            const double t = original.range.lo + original.range.length() * i / 10000.;
            const double y = std::abs(plane.toLocal(original.curve->point(t)).y());
            if (y > height) {
                height = y;
                peak = t;
            }
        }
        const auto witness = original.curve->point(peak) + .5 * (lower + upper) * normal;
        double geometry = INFINITY;
        for (auto id : walls)
            geometry = std::min(geometry, projectPoint(*body.face(id).surface, witness, original.range,
                                                       {0, options.thickness})
                                              .distance);
        FK_CHECK(geometry <= 1e-9);
    };
    check(result);
    if (roundtrip)
        check(readBodyBinary(writeBodyBinary(result)));
    FK_CHECK(writeBodyBinary(source) == before);
}
ThickenOptions options(ThickenSide side, double scale = 1) {
    ThickenOptions result;
    result.thickness = .1 * scale;
    result.side = side;
    result.tolerance = 1e-6;
    result.offsetTolerance = 1e-7;
    return result;
}
} // namespace

FK_TEST(PlanarThickenHiddenPolynomialRationalAllSides) {
    for (bool rational : {false, true})
        for (auto side : {ThickenSide::Forward, ThickenSide::Backward, ThickenSide::Both})
            verify(fixture(1, rational, Frame3(), true), options(side));
}

FK_TEST(PlanarThickenPoseScaleSensePreservesWallGeometryAndPcurves) {
    for (double scale : {.01, 1., 1000.})
        for (bool rational : {false, true})
            for (bool sense : {false, true}) {
                const Frame3 frame(scale * Vec3(2, -3, 1), Vec3(.2, .3, 1), Vec3(1, 0, 0));
                try {
                    verify(fixture(scale, rational, frame, sense), options(ThickenSide::Both, scale));
                } catch (const std::exception &e) {
                    throw std::runtime_error(std::string(e.what()) + " scale=" + std::to_string(scale) +
                                             " rational=" + std::to_string(rational) +
                                             " sense=" + std::to_string(sense));
                }
            }
}

FK_TEST(PlanarThickenNestedTrimReverseTransformedHolesAndRoundtrip) {
    for (bool rational : {false, true})
        for (bool reverse : {false, true}) {
            const Frame3 frame({2, -3, 1}, Vec3(.2, .3, 1), Vec3(1, 0, 0));
            auto source = fixture(1, rational, frame, !reverse, true, reverse, true);
            for (auto id : source.edges()) {
                auto fin = source.fins().front();
                for (auto use : source.fins())
                    if (source.fin(use).edge == id)
                        fin = use;
                source.edge(id).curve = std::make_shared<TransformedCurve>(
                    embedCurve(source.fin(fin).pcurve, Frame3()), Transform3::fromFrame(frame));
            }
            verify(source, options(ThickenSide::Both), true);
        }
}

FK_TEST(PlanarThickenDirectionControlRetainsOriginalCurve) {
    for (bool rational : {false, true}) {
        auto setting = options(ThickenSide::Both);
        setting.useDirection = true;
        verify(fixture(1, rational, Frame3(), true), setting);
    }
}

FK_TEST(PlanarThickenInvalidAndCancellationRetainLegacyHistoryReset) {
    const auto source = fixture(1, false, Frame3(), true);
    const auto before = writeBodyBinary(source);
    for (double thickness :
         {0., -1., std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(), .1}) {
        auto setting = options(ThickenSide::Forward);
        setting.thickness = thickness;
        BodyHistory history(1, {1}, {4}, {4}, 2, 3, 4);
        const bool cancel = thickness == .1;
        auto token = std::make_shared<std::atomic<bool>>(cancel);
        CancellationScope scope(token);
        bool rejected = false;
        try {
            thickenSheet(source, {}, setting, &history);
        } catch (const OperationCancelled &) {
            rejected = cancel;
        } catch (const std::exception &) {
            rejected = !cancel;
        }
        FK_CHECK(rejected && history.operandCount() == 0);
        FK_CHECK(writeBodyBinary(source) == before);
    }
}

FK_TEST(PlanarThickenClosedNativeNurbsAndAngularCircleCompatibility) {
    const Frame3 frame({2, -3, 1}, Vec3(.2, .3, 1), Vec3(1, 0, 0));
    for (bool nurbs : {false, true}) {
        CurvePtr<2> uv = std::make_shared<Circle<2>>(Vec2(), Vec2(1, 0), Vec2(0, 1), 1);
        if (nurbs) {
            const double w = std::sqrt(.5);
            uv = std::make_shared<BSplineCurve<2>>(
                2, std::vector<double>{0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 4},
                std::vector<Vec2>{
                    {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1}, {1, 0}},
                std::vector<double>{1, w, 1, w, 1, w, 1, w, 1});
        }
        const Interval range = nurbs ? Interval{0, 4} : Interval{0, kTwoPi};
        const Vec3 point = frame.toGlobal({1, 0, 0});
        Body::BuildFace face;
        face.surface = std::make_shared<Plane>(frame);
        face.loops = {{{0, true, uv, 1e-9}}};
        const auto source = Body::buildSheet({point}, {{0, 0, embedCurve(uv, frame), range, 1e-9}}, {face});
        verify(source, options(ThickenSide::Both), true);
    }
}
