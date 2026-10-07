#include <BRepGProp.hxx>
#include <BRepOffsetAPI_ThruSections.hxx>
#include <GProp_GProps.hxx>

#include <cmath>

#include "fk_body_check.h"
#include "fk_bspline_surface.h"
#include "fk_loft.h"
#include "fk_mass.h"
#include "fk_offset.h"
#include "fk_tessellate.h"
#include "fk_test_profiles.h"

using namespace fktest;

namespace {

LoftSection circleAt(double z, double r, const Vec3 &shift = Vec3()) {
    LoftSection s;
    s.frame = Frame3(Vec3(0, 0, z) + shift, Vec3(0, 0, 1), Vec3(1, 0, 0));
    s.loop.segments = {arcSegment(Vec2(0, 0), r, 0.0, kTwoPi)};
    return s;
}

LoftSection squareAt(double z, double a, bool clockwise = false) {
    LoftSection s;
    s.frame = Frame3(Vec3(0, 0, z), Vec3(0, 0, 1), Vec3(1, 0, 0));
    std::vector<Vec2> c{Vec2(a / 2, 0), Vec2(a / 2, a / 2), Vec2(-a / 2, a / 2), Vec2(-a / 2, -a / 2), Vec2(a / 2, -a / 2)};
    // Parte a meta' del lato destro (come il cerchio ad angolo 0).
    std::vector<ProfileSegment> segments;
    for (std::size_t i = 0; i < c.size(); ++i) segments.push_back(lineSegment(c[i], c[(i + 1) % c.size()]));
    s.loop.segments = segments;
    if (clockwise) s.loop = reversed(s.loop);
    return s;
}

MassProperties checkedSolid(const Body &body) {
    for (const CheckIssue &issue : checkBody(body)) reportFailure(__FILE__, __LINE__, describe(issue.code) + ": " + issue.message);
    FK_CHECK(!body.isSheet());
    TessellationOptions options;
    options.deflection = 0.01;
    FK_CHECK(tessellate(body, options).failedFaces == 0);
    return massProperties(body, 1e-12);
}

double occtLoftVolume(const std::vector<LoftSection> &sections, bool ruled) {
    BRepOffsetAPI_ThruSections loft(true, ruled, 1e-7);
    for (const LoftSection &s : sections) loft.AddWire(occtWire(s.loop, s.frame));
    loft.Build();
    GProp_GProps props;
    BRepGProp::VolumeProperties(loft.Shape(), props, 1e-9);
    return std::fabs(props.Mass());
}

}

FK_TEST(LoftRuledFrustums) {
    // Tronco di cono (cerchi coassiali) e di piramide: volumi esatti e OCCT.
    const double R = 2.0, r = 1.0, h = 3.0;
    const std::vector<LoftSection> cone{circleAt(0.0, R), circleAt(h, r)};
    const double coneVolume = kPi * h / 3.0 * (R * R + R * r + r * r);
    FK_CHECK_NEAR(checkedSolid(loftSolid(cone, true)).volume, coneVolume, 1e-10 * coneVolume);
    FK_CHECK_NEAR(occtLoftVolume(cone, true), coneVolume, 1e-6 * coneVolume);
    // Il verso dei loop non conta (uno orario).
    const std::vector<LoftSection> pyramid{squareAt(0.0, 4.0), squareAt(h, 2.0, true)};
    const double pyramidVolume = h / 3.0 * (16.0 + 4.0 + 8.0);
    const MassProperties m = checkedSolid(loftSolid(pyramid, true));
    FK_CHECK_NEAR(m.volume, pyramidVolume, 1e-10 * pyramidVolume);
    FK_CHECK_NEAR(occtLoftVolume(pyramid, true), pyramidVolume, 1e-9 * pyramidVolume);
    // Tre sezioni rigate: due tronchi.
    const std::vector<LoftSection> three{circleAt(0.0, R), circleAt(h, r), circleAt(2 * h, R)};
    FK_CHECK_NEAR(checkedSolid(loftSolid(three, true)).volume, 2.0 * coneVolume, 1e-10 * coneVolume);
}

FK_TEST(LoftSmoothThroughCircles) {
    // Tre cerchi coassiali: raggio R(z) = 2 - z + z^2 / 4 (quadratica), V = 112 pi / 15.
    const std::vector<LoftSection> sections{circleAt(0.0, 2.0), circleAt(2.0, 1.0), circleAt(4.0, 2.0)};
    const MassProperties m = checkedSolid(loftSolid(sections, false));
    FK_CHECK_NEAR(m.volume, 112.0 * kPi / 15.0, 1e-10 * m.volume);
    FK_CHECK(std::hypot(m.centroid.x(), m.centroid.y()) < 1e-10);
    FK_CHECK_NEAR(m.centroid.z(), 2.0, 1e-10);
    // Due sezioni lisce = rigate.
    FK_CHECK_NEAR(checkedSolid(loftSolid({circleAt(0.0, 2.0), circleAt(3.0, 1.0)}, false)).volume, kPi * (4.0 + 2.0 + 1.0), 1e-10 * 22.0);
}

// Le campate razionali di un'ellisse ruotata hanno lunghezze diverse.
// Usarle come nuovi intervalli parametrici introduceva spigoli interni
// nel loft e trasformava due facce laterali in sei durante l'offset.
FK_TEST(LoftEllipsePreservesSmoothOffsetFaces) {
    auto middle = circleAt(10.0, 1.0);
    const auto ellipse = std::make_shared<Ellipse<2>>(Vec2(), Vec2(1,0), Vec2(0,1), 5.0, 2.5);
    middle.loop.segments = {{ellipse, {0.17, 0.17 + kTwoPi}}};
    const Body body = loftSolid({circleAt(0.0, 2.5), middle, circleAt(20.0, 4.0)}, false);
    checkedSolid(body);
    std::vector<FaceId> sides;
    for (FaceId f : body.faces()) {
        const auto &surface = *body.face(f).surface;
        if (surface.type() != SurfaceType::BSpline) continue;
        sides.push_back(f);
        const auto &spline = static_cast<const BSplineSurface &>(surface);
        const auto patches = spline.cachedBezierPatches();
        const auto us = spline.uBreakpoints(spline.uDomain());
        const auto vs = spline.vBreakpoints(spline.vDomain());
        const std::size_t nv = vs.size() - 1;
        for (std::size_t i = 1; i + 1 < us.size(); ++i)
            for (std::size_t j = 0; j < nv; ++j)
                for (double fraction : {0.25, 0.5, 0.75}) {
                    const double v = vs[j] + fraction * (vs[j+1] - vs[j]);
                    FK_CHECK_NEAR(distance((*patches)[(i-1)*nv+j].normal(us[i],v),
                                           (*patches)[i*nv+j].normal(us[i],v)), 0.0, 1e-8);
                }
    }
    FK_CHECK(sides.size() == 2);
    for (double d : {-0.5, 1.0}) {
        const auto result = offsetFaces(body, sides, d, 1e-7, true);
        FK_CHECK(result.body.faces().size() == sides.size());
        FK_CHECK(checkBody(result.body).empty());
        FK_CHECK(result.notes.empty());

    }
}

FK_TEST(LoftCircleToSquare) {
    // Sezioni diverse: il cerchio si divide nei punti del quadrato. Confronto con OCCT.
    for (bool ruled : {true, false}) {
        const std::vector<LoftSection> sections = ruled ? std::vector<LoftSection>{circleAt(0.0, 1.5), squareAt(3.0, 2.0)}
                                                        : std::vector<LoftSection>{circleAt(0.0, 1.5), squareAt(2.0, 2.0), circleAt(4.0, 1.0, Vec3(0.5, 0, 0))};
        const Body body = loftSolid(sections, ruled);
        const MassProperties m = checkedSolid(body);
        FK_CHECK(m.volume > 0.0);
        // OCCT sceglie la stessa corrispondenza ma interpola diversamente tra le sezioni.
        const double reference = occtLoftVolume(sections, ruled);
        FK_CHECK_NEAR(m.volume, reference, 0.02 * reference);
    }
}

FK_TEST(LoftGuideCrossesEverySection) {
    const std::vector<LoftSection> sections{circleAt(0.0, 2.0), circleAt(3.0, 1.0)};
    LoftOptions options;
    options.ruled = true;
    const Vec3 a(2, 0, 0), b(1, 0, 3), direction = normalized(b - a);
    options.guides.push_back({{std::make_shared<Line<3>>(a, direction), {0.0, distance(a, b)}}});
    const Body guided = loftSolid(sections, options);
    FK_CHECK_NEAR(checkedSolid(guided).volume, kPi * (4.0 + 2.0 + 1.0), 1e-10 * 22.0);

    LoftOptions invalid = options;
    invalid.guides.front().front() = {std::make_shared<Line<3>>(Vec3(4, 0, 0), Vec3(0, 0, 1)), {0.0, 3.0}};
    bool failed = false;
    try {
        (void)loftSolid(sections, invalid);
    } catch (const std::domain_error &) {
        failed = true;
    }
    FK_CHECK(failed);
}

FK_TEST(LoftGuideInfluenceAndEndContinuity) {
    const std::vector<LoftSection> sections{circleAt(0.0, 5.0), circleAt(1.0, 1.0), circleAt(4.0, 1.1)};
    const Vec3 p[3] = {Vec3(5, 0, 0), Vec3(1, 0, 1), Vec3(1.1, 0, 4)};
    LoftOptions free, guided;
    for (int i = 0; i < 2; ++i) {
        const Vec3 direction = normalized(p[i + 1] - p[i]);
        free.guides.resize(1);
        free.guides.front().push_back({std::make_shared<Line<3>>(p[i], direction), {0.0, distance(p[i], p[i + 1])}});
    }
    guided = free;
    free.guideInfluence = 0.0;
    guided.guideInfluence = 1.0;
    const double freeVolume = checkedSolid(loftSolid(sections, free)).volume;
    const double guidedVolume = checkedSolid(loftSolid(sections, guided)).volume;
    FK_CHECK(std::fabs(guidedVolume - freeVolume) > 1e-3 * freeVolume);

    guided.guideContinuity = 1;
    const double tangentOnlyVolume = checkedSolid(loftSolid(sections, guided)).volume;
    guided.guideContinuity = 2;
    const double curvatureVolume = checkedSolid(loftSolid(sections, guided)).volume;
    FK_CHECK(std::fabs(tangentOnlyVolume - curvatureVolume) > 1e-8 * tangentOnlyVolume);

    guided.startContinuity = 1;
    guided.endContinuity = 2;
    guided.startInfluence = 0.7;
    guided.endInfluence = 0.8;
    FK_CHECK(checkedSolid(loftSolid(sections, guided)).volume > 0.0);
}

FK_TEST(LoftGuideG0DoesNotEnableDerivativeConstraints) {
    const std::vector<LoftSection> sections{circleAt(0.0, 1.0), circleAt(2.0, 2.0), circleAt(4.0, 1.0)};
    LoftOptions options;
    options.guideContinuity = 0;
    options.guideInfluence = 1.0;
    options.guides.push_back({{std::make_shared<BSplineCurve<3>>(2, std::vector<double>{0, 0, 0, 1, 1, 1},
                                                                  std::vector<Vec3>{{1, 0, 0}, {3, 0, 2}, {1, 0, 4}}),
                               {0.0, 1.0}}});
    const Body body = loftSolid(sections, options);
    checkedSolid(body);
    for (FaceId face : body.faces()) {
        if (body.face(face).surface->type() != SurfaceType::BSpline) continue;
        const auto &surface = static_cast<const BSplineSurface &>(*body.face(face).surface);
        FK_CHECK(surface.vDegree() == 2);
    }
}

FK_TEST(LoftTangencyAppliesToEveryGuide) {
    const std::vector<LoftSection> sections{circleAt(0.0, 1.0), circleAt(2.0, 2.0), circleAt(4.0, 1.0)};
    LoftOptions options;
    options.guideContinuity = 1;
    options.guideInfluence = 1.0;
    const auto right = std::make_shared<BSplineCurve<3>>(2, std::vector<double>{0, 0, 0, 1, 1, 1},
                                                          std::vector<Vec3>{{1, 0, 0}, {3, 0, 2}, {1, 0, 4}});
    // La seconda guida cambia posizione relativa sul perimetro: a meta' loft
    // incontra il cerchio a 120 gradi, mentre sulle estremita' e' a 180.
    const auto left = std::make_shared<BSplineCurve<3>>(2, std::vector<double>{0, 0, 0, 1, 1, 1},
                                                         std::vector<Vec3>{{-1, 0, 0}, {-1, 2 * std::sqrt(3.0), 2}, {-1, 0, 4}});
    options.guides = {{{right, {0.0, 1.0}}}, {{left, {0.0, 1.0}}}};
    const Body body = loftSolid(sections, options);
    checkedSolid(body);
    for (const auto &expected : std::vector<std::pair<Vec3, Vec3>>{{Vec3(1, 0, 0), right->derivative(0.0)},
                                                                    {Vec3(-1, 0, 0), left->derivative(0.0)}}) {
        bool found = false;
        for (EdgeId edgeId : body.edges()) {
            const Edge &edge = body.edge(edgeId);
            if (distance(edge.curve->point(edge.range.lo), expected.first) > 1e-8
                || distance(edge.curve->point(edge.range.hi), expected.first + Vec3(0, 0, 4)) > 1e-8)
                continue;
            found = true;
            FK_CHECK(norm(cross(normalized(edge.curve->derivative(edge.range.lo)), normalized(expected.second))) < 1e-8);
        }
        FK_CHECK(found);
    }
}

FK_TEST(LoftSheet) {
    // Due segmenti paralleli: rettangolo; con una catena girata al contrario si riallinea.
    LoftSection a, b;
    a.frame = Frame3(Vec3(0, 0, 0), Vec3(0, 1, 0), Vec3(1, 0, 0));
    a.loop.segments = {lineSegment(Vec2(0, 0), Vec2(2, 0))};
    b.frame = Frame3(Vec3(0, 3, 0), Vec3(0, 1, 0), Vec3(1, 0, 0));
    b.loop.segments = {lineSegment(Vec2(2, 0), Vec2(0, 0))};
    const Body sheet = loftSheet({a, b}, true);
    FK_CHECK(sheet.isSheet());
    for (const CheckIssue &issue : checkBody(sheet)) reportFailure(__FILE__, __LINE__, describe(issue.code) + ": " + issue.message);
    double area = 0.0;
    for (FaceId f : sheet.faces()) area += faceArea(sheet, f);
    FK_CHECK_NEAR(area, 6.0, 1e-10);
}
