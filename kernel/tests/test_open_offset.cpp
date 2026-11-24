#include "fk_test.h"
#include "fk_body_io.h"
#include "fk_body_check.h"
#include "fk_bspline_surface.h"
#include "fk_classify.h"
#include "fk_mass.h"
#include "fk_offset.h"
#include "fk_profile.h"
#include "fk_primitives.h"
#include "fk_revolve.h"
#include "fk_thicken.h"
#include "fk_offset_support.h"
#include "fk_exchange.h"
#include "fk_tessellate.h"
#include <fstream>
#include <iterator>

using namespace ForgeCad::Kernel;
namespace {
Body groove() {
    ProfileLoop chain;
    const std::vector<Vec2> points{{25, 0}, {25, 5}, {23, 5}, {23, 10}, {24, 10}, {24, 20}};
    for (std::size_t i = 1; i < points.size(); ++i)
        chain.segments.push_back(
            {std::make_shared<Line<2>>(points[i - 1], points[i] - points[i - 1]), {0., distance(points[i - 1], points[i])}});
    return makeSheetRevolution(Frame3{}, {chain}, kTwoPi);
}
Body perforatedTray(double excursion = 0.) {
    // Il fondo bicubico resta coincidente con i lati sul perimetro.
    // Il rilievo asimmetrico rende non planari i tre contorni interni.
    const auto box = makeBox(Frame3{}, 30., 30., 12.);
    const std::vector<double> knots{0., 0., 0., 0., 30., 30., 30., 30.};
    std::vector<Vec3> poles;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            poles.emplace_back(10. * i, 10. * j, (i == 1 && j == 2) ? .4 : 0.);
    const auto floor = std::make_shared<BSplineSurface>(3, 3, knots, knots, 4, 4, poles);
    std::vector<Vec3> points;
    for (auto vertex : box.vertices()) points.push_back(box.vertex(vertex).point);
    std::vector<Body::BuildEdge> edges;
    for (auto id : box.edges()) {
        const auto &edge = box.edge(id);
        edges.push_back({box.edgeStart(id).index, box.edgeEnd(id).index, edge.curve, edge.range, edge.tolerance});
    }
    std::vector<Body::BuildFace> faces;
    for (auto id : box.faces()) {
        const auto &face = box.face(id);
        const auto *plane = dynamic_cast<const Plane *>(face.surface.get());
        const double vertical = (face.sense ? 1. : -1.) * plane->frame().zDir().z();
        if (vertical > .5) continue;
        const bool bottom = vertical < -.5;
        auto &copy = faces.emplace_back(Body::BuildFace{bottom ? SurfacePtr(floor) : face.surface,
                                                       bottom ? false : face.sense, {}});
        for (auto loop : face.loops) {
            auto &contour = copy.loops.emplace_back();
            for (auto fid : box.loopFins(loop)) {
                const auto &fin = box.fin(fid);
                const auto &edge = box.edge(fin.edge);
                CurvePtr<2> uv = fin.pcurve;
                if (bottom) {
                    const auto a = edge.curve->point(edge.range.lo), b = edge.curve->point(edge.range.hi);
                    uv = std::make_shared<Line<2>>(Vec2{a.x(), a.y()}, Vec2{b.x() - a.x(), b.y() - a.y()});
                }
                contour.push_back({fin.edge.index, fin.sense, uv, 0.});
            }
        }
        if (!bottom) continue;
        for (double x : {excursion == 0. ? 5. : 2.4, 12., 19.}) {
            const std::vector<Vec2> corners{{x, 11.}, {x + 4., 11.}, {x + 4., 20.}, {x, 20.}};
            const int base = int(points.size());
            for (const auto &p : corners) points.push_back(floor->point(p.x(), p.y()));
            auto &hole = copy.loops.emplace_back();
            for (int i = 0; i < 4; ++i) {
                const int next = (i + 1) % 4;
                const auto a = corners[i], b = corners[next];
                const Interval range{0., distance(a, b)};
                CurvePtr<2> uv = std::make_shared<Line<2>>(a, b - a);
                if (x == 2.4 && i == 3) {
                    uv = std::make_shared<BSplineCurve<2>>(3,
                        std::vector<double>{0., 0., 0., 0., range.hi, range.hi, range.hi, range.hi},
                        std::vector<Vec2>{a, a + (b - a) / 3. - Vec2{excursion / 6., 0.},
                            a + 2. * (b - a) / 3. + Vec2{excursion / 6., 0.}, b});
                }
                const auto edge = fitCurve([&](double t) {
                    const auto p = uv->point(t);
                    return floor->point(p.x(), p.y());
                }, range, uv->breakpoints(range), 1e-10);
                hole.push_back({int(edges.size()), true, uv, 0.});
                edges.push_back({base + i, base + next, edge, range, 0.});
            }
        }
    }
    return Body::buildSheet(points, edges, faces);
}
void valid(const Body &body, bool solid) {
    FK_CHECK(body.isSheet() != solid);
    for (const auto &issue : checkBody(body))
        fktest::reportFailure(__FILE__, __LINE__, issue.message);
    if (solid)
        for (auto edge : body.edges())
            FK_CHECK(!body.isLaminar(edge));
}
void declared(const Body &body, const BodyHistory &history, int inputFaces) {
    for (auto f : body.faces()) {
        FK_CHECK(history.isDeclared(EntityKind::Face, f.index));
        FK_CHECK(!history.origins(EntityKind::Face, f.index).empty());
        for (const auto &link : history.origins(EntityKind::Face, f.index))
            FK_CHECK(link.operand == 0 && link.kind == EntityKind::Face && link.index >= 0 && link.index < inputFaces);
    }
}
} // namespace
FK_TEST(OpenOffsetGrooveExactCollapse) {
    const auto sheet = groove();
    const auto before = writeBodyBinary(sheet);
    auto offset = offsetFaces(sheet, sheet.faces(), 2.5, linearResolution(), true);
    valid(offset.body, false);
    FK_CHECK(offset.body.faces().size() == 3);
    declared(offset.body, offset.history, int(sheet.faces().size()));
    ThickenOptions options;
    options.thickness = 2.5;
    BodyHistory history;
    const auto solid = thickenSheet(sheet, {}, options, &history);
    valid(solid, true);
    FK_CHECK(solid.faces().size() == 10);
    declared(solid, history, int(sheet.faces().size()));
    FK_CHECK_NEAR(massProperties(solid).volume, kPi * 2920, 1e-6);
    FK_CHECK(writeBodyBinary(sheet) == before);
    options.thickness = 2.49;
    valid(thickenSheet(sheet, {}, options), true);
    options.thickness = 2.51;
    FK_CHECK_THROWS(thickenSheet(sheet, {}, options));
    options.thickness = 2.5;
    options.side = ThickenSide::Backward;
    const auto inward = thickenSheet(sheet, {}, options);
    valid(inward, true);
    FK_CHECK_NEAR(massProperties(inward).volume, kPi * 2595, 1e-6);
}
FK_TEST(OpenOffsetShowerHeadTwoPointFive) {
    std::ifstream file(FORGEKERNEL_TEST_DATA_DIR "/shower_head_sheet.body", std::ios::binary);
    if (!file)
        throw std::runtime_error("fixture Shower Head assente");
    const std::string data((std::istreambuf_iterator<char>(file)), {});
    const auto sheet = readBodyBinary(data);
    const auto before = writeBodyBinary(sheet);
    FK_CHECK(sheet.faces().size() == 13);
    valid(sheet, false);
    ThickenOptions options;
    options.thickness = 2.5;
    BodyHistory history;
    const auto solid = thickenSheet(sheet, {}, options, &history);
    valid(solid, true);
    FK_CHECK(solid.faces().size() == 26);
    declared(solid, history, 13);
    SolidClassifier classifier(solid, linearResolution());
    int checked = 0;
    for (auto f : sheet.faces()) {
        const auto &face = sheet.face(f);
        if (face.surface->type() != SurfaceType::BSpline)
            continue;
        const auto u = face.surface->uDomain(), v = face.surface->vDomain();
        const double a = (u.lo + u.hi) * .5, b = (v.lo + v.hi) * .5;
        const auto p = face.surface->point(a, b), n = (face.sense ? 1. : -1.) * face.surface->normal(a, b);
        FK_CHECK(classifier.classify(p + 1.25 * n) == PointLocation::Inside);
        FK_CHECK(classifier.classify(p - 1.25 * n) == PointLocation::Outside);
        FK_CHECK(classifier.classify(p + 3.75 * n) == PointLocation::Outside);
        ++checked;
    }
    FK_CHECK(checked == 4);
    const auto offset = offsetFaces(sheet, sheet.faces(), 2.5, linearResolution(), true);
    valid(offset.body, false);
    FK_CHECK(offset.body.faces().size() == 11);
    declared(offset.body, offset.history, 13);
    int fitted = 0;
    for (auto f : offset.body.faces()) {
        const auto &target = offset.body.face(f);
        if (target.surface->type() != SurfaceType::BSpline)
            continue;
        const auto &origin = offset.history.origins(EntityKind::Face, f.index);
        FK_CHECK(origin.size() == 1);
        const auto &source = sheet.face(FaceId{origin.front().index});
        for (double u : {.2, .5, .8})
            for (double v : {.1, .4, .7}) {
                const auto expected = source.surface->point(u, v) + 2.5 * (source.sense ? 1. : -1.) * source.surface->normal(u, v);
                FK_CHECK(distance(target.surface->point(u, v), expected) < 2 * linearResolution());
            }
        ++fitted;
    }
    FK_CHECK(fitted == 4);
    FK_CHECK(writeBodyBinary(sheet) == before);
}

FK_TEST(OpenOffsetThickenCurvedFloorWithThreeSlots) {
    const auto sheet = perforatedTray();
    const auto before = writeBodyBinary(sheet);
    FK_CHECK(checkBody(sheet, {true, true}).empty());
    ThickenOptions options;
    options.thickness = 2.;
    options.side = ThickenSide::Backward;
    BodyHistory history;
    const auto solid = thickenSheet(sheet, {}, options, &history);
    valid(solid, true);
    FK_CHECK(solid.shells().size() == 1);
    FK_CHECK(checkBody(solid, {true, true}).empty());
    for (auto face : sheet.faces()) {
        FK_CHECK(history.descendants(EntityKind::Face, 0, face.index).size() == 2);
        for (std::size_t i = 1; i < sheet.face(face).loops.size(); ++i)
            for (auto fin : sheet.loopFins(sheet.face(face).loops[i]))
                FK_CHECK(history.generated(EntityKind::Face, EntityKind::Edge, 0, sheet.fin(fin).edge.index).size() == 1);
    }
    SolidClassifier classify(solid, linearResolution());
    for (double x : {7., 14., 21.})
        for (double z : {.2, 1., 1.8})
            FK_CHECK(classify.classify({x, 15., z}) == PointLocation::Outside);
    for (double x : {4., 10., 17., 26.})
        for (double z : {.2, 1., 1.8})
            FK_CHECK(classify.classify({x, 15., z}) == PointLocation::Inside);
    FK_CHECK(writeBodyBinary(sheet) == before);
}

FK_TEST(OpenOffsetContinuationPreservesSource) {
    // Patch razionale: la continuazione deve mantenere anche la derivata
    // della normale, non soltanto la posizione al bordo.
    std::vector<Vec3> poles;
    std::vector<double> weights;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            poles.emplace_back(i, j, .08 * i * i + .04 * j * j + .03 * i * j);
            weights.push_back(1. + .04 * i + .02 * j);
        }
    const BSplineSurface source(3, 3, {0, 0, 0, 0, 1, 1, 1, 1}, {0, 0, 0, 0, 1, 1, 1, 1}, 4, 4, poles, weights);
    const auto continued = detail::continueOffsetSupport(source, {-.05, 1.05}, {-.05, 1.05});
    for (double u : {0., .13, .51, .86, 1.})
        for (double v : {0., .19, .47, .91, 1.}) {
            Vec3 a[9], b[9];
            source.evaluate(u, v, 2, a);
            continued.evaluate(u, v, 2, b);
            for (int k = 0; k <= 2; ++k)
                for (int l = 0; l + k <= 2; ++l)
                    FK_CHECK(distance(a[Surface::derivativeIndex(k, l, 2)], b[Surface::derivativeIndex(k, l, 2)]) < 1e-10);
        }
    for (bool fixedU : {false, true})
        for (double boundary : {0., 1.}) {
            const auto curve = fixedU ? continued.vIsoCurve(.41) : continued.uIsoCurve(.41);
            Vec3 left[3], right[3];
            curve.evaluateLeft(boundary, 2, left);
            curve.evaluate(boundary, 2, right);
            for (int k = 0; k <= 2; ++k)
                FK_CHECK(distance(left[k], right[k]) < 1e-9);
        }
}
FK_TEST(OpenOffsetShowerHeadInwardPreservesExterior) {
    std::ifstream file(FORGEKERNEL_TEST_DATA_DIR "/shower_head_sheet.body", std::ios::binary);
    if (!file)
        throw std::runtime_error("fixture Shower Head assente");
    const std::string data((std::istreambuf_iterator<char>(file)), {});
    const auto sheet = readBodyBinary(data);
    const auto before = writeBodyBinary(sheet);
    ThickenOptions options;
    options.thickness = 2.5;
    options.side = ThickenSide::Backward;
    BodyHistory history;
    const auto solid = thickenSheet(sheet, {}, options, &history);
    valid(solid, true);
    FK_CHECK(solid.faces().size() == 28);
    declared(solid, history, 13);
    for (auto source : sheet.faces()) {
        int exact = 0;
        for (auto target : solid.faces())
            if (solid.face(target).surface == sheet.face(source).surface) {
                FK_CHECK(solid.face(target).sense == sheet.face(source).sense);
                ++exact;
            }
        FK_CHECK(exact == 1);
    }
    SolidClassifier classifier(solid, linearResolution());
    for (auto f : sheet.faces()) {
        const auto &face = sheet.face(f);
        if (face.surface->type() != SurfaceType::BSpline)
            continue;
        const auto p = face.surface->point(.5, .5), n = (face.sense ? 1. : -1.) * face.surface->normal(.5, .5);
        FK_CHECK(classifier.classify(p - 1.25 * n) == PointLocation::Inside);
        FK_CHECK(classifier.classify(p + 1.25 * n) == PointLocation::Outside);
        FK_CHECK(classifier.classify(p - 3.75 * n) == PointLocation::Outside);
    }
    TessellationOptions mesh;
    mesh.deflection = .1;
    FK_CHECK(tessellate(solid, mesh).failedFaces == 0);
    const auto offset = offsetFaces(sheet, sheet.faces(), -2.5, linearResolution(), true);
    valid(offset.body, false);
    FK_CHECK(offset.body.faces().size() == 13);
    declared(offset.body, offset.history, 13);
    int fitted = 0;
    for (auto f : offset.body.faces()) {
        const auto &target = offset.body.face(f);
        if (target.surface->type() != SurfaceType::BSpline)
            continue;
        const auto &origin = offset.history.origins(EntityKind::Face, f.index);
        FK_CHECK(origin.size() == 1);
        const auto &source = sheet.face(FaceId{origin.front().index});
        for (double u : {.2, .5, .8})
            for (double v : {.1, .4, .7}) {
                const auto expected = source.surface->point(u, v) - 2.5 * (source.sense ? 1. : -1.) * source.surface->normal(u, v);
                FK_CHECK(distance(target.surface->point(u, v), expected) < 2 * linearResolution());
            }
        ++fitted;
    }
    FK_CHECK(fitted == 4);
    FK_CHECK(writeBodyBinary(sheet) == before);
}

FK_TEST(OpenOffsetRejectsOnlyRetainedFold) {
    // S(u,v)=(u,v,u²/2): raggio di curvatura unitario a u=0 e
    // maggiore di due a u>=1. Il tratto u<1 puo' essere eliminato dal trim.
    std::vector<Vec3> poles;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 2; ++j)
            poles.emplace_back(.75 * i, j, i == 2 ? 1.125 : 0.);
    const auto surface =
        std::make_shared<BSplineSurface>(2, 1, std::vector<double>{0, 0, 0, 1.5, 1.5, 1.5}, std::vector<double>{0, 0, 1, 1}, 3, 2, poles);
    const auto validate = [&](double lo) {
        detail::RawModel model;
        for (Vec2 uv : {Vec2(lo, 0), Vec2(1.5, 0), Vec2(1.5, 1), Vec2(lo, 1)})
            model.points.push_back(surface->point(uv.x(), uv.y()));
        model.edges = {{0, 1, surface->vIso(0), true, {lo, 1.5}},
                       {1, 2, surface->uIso(1.5), true, {0, 1}},
                       {3, 2, surface->vIso(1), true, {lo, 1.5}},
                       {0, 3, surface->uIso(lo), true, {0, 1}}};
        detail::RawFace face;
        face.surface = surface;
        face.loops = {{{0, true, std::make_shared<Line<2>>(Vec2(0, 0), Vec2(1, 0))},
                       {1, true, std::make_shared<Line<2>>(Vec2(1.5, 0), Vec2(0, 1))},
                       {2, false, std::make_shared<Line<2>>(Vec2(0, 1), Vec2(1, 0))},
                       {3, false, std::make_shared<Line<2>>(Vec2(lo, 0), Vec2(0, 1))}}};
        detail::requireRegularOffsetTrim(model, face, 2., {0, 1.5}, {0, 1}, linearResolution());
    };
    FK_CHECK_THROWS(validate(0.));
    validate(1.);
}

FK_TEST(OpenOffsetTrimExcursion) {
    ThickenOptions options;
    options.thickness = 2.;
    options.side = ThickenSide::Backward;
    const auto contained = perforatedTray(4.);
    FK_CHECK(checkBody(contained, {true, true}).empty());
    valid(thickenSheet(contained, {}, options), true);

    // Gli estremi e il punto medio UV restano identici. Aumentando
    // l'escursione cubica, il foro attraversa il bordo rifilato del fondo.
    const auto crossing = perforatedTray(8.);
    FK_CHECK(checkBody(crossing, {true, true}).empty());
    bool rejectedAtTrim = false;
    try {
        (void)thickenSheet(crossing, {}, options);
    } catch (const std::domain_error &error) {
        rejectedAtTrim = std::string(error.what()) == "spessore: il foro incontra il bordo del supporto rifilato";
    }
    FK_CHECK(rejectedAtTrim);
}
