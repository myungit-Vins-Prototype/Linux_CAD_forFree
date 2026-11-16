#include "fk_body_check.h"
#include "fk_helix.h"
#include "fk_mass.h"
#include "fk_nurbs.h"
#include "fk_sweep.h"
#include "fk_test.h"
using namespace ForgeCad::Kernel;
using namespace fktest;
namespace {
// Costruttori locali: questo target non dipende dagli helper OCCT.
ProfileSegment lineSegment(const Vec2 &a, const Vec2 &b) {
    return {std::make_shared<Line<2>>(a, b - a), {0.0, distance(a, b)}};
}
ProfileSegment arcSegment(const Vec2 &center, double radius, double from, double to) {
    return {std::make_shared<Circle<2>>(makeCircle(center, radius)), {from, to}};
}
ProfileRegion circleRegion(double r, double hole = 0.0) {
    ProfileRegion region;
    region.outer.segments = {arcSegment(Vec2(0, 0), r, 0.0, kTwoPi)};
    if (hole > 0.0)
        region.holes.push_back(reversed(ProfileLoop{{arcSegment(Vec2(0, 0), hole, 0.0, kTwoPi)}}));
    return region;
}
} // namespace
namespace {
ProfileRegion k2Disk(const Vec2 &center, double radius, double hole = 0.0) {
    ProfileRegion p;
    p.outer.segments = {arcSegment(center, radius, 0, kTwoPi)};
    if (hole > 0)
        p.holes = {reversed(ProfileLoop{{arcSegment(center, hole, 0, kTwoPi)}})};
    return p;
}
} // namespace

FK_TEST(SweepRejectsGlobalTurnOverlap) {
    for (double scale : {0.01, 1.0, 10.0})
        for (int pose = 0; pose < 2; ++pose) {
            HelixSpec spec;
            spec.radius = scale;
            spec.pitch = .1 * scale;
            spec.turns = 2;
            if (pose)
                spec.frame = Frame3(Vec3(13, -7, 5), normalized(Vec3(1, 2, 3)), Vec3(0, 1, 0));
            const auto h = std::make_shared<HelixCurve>(spec);
            const Frame3 start(h->point(0), h->derivative(0), spec.frame.xDir());
            FK_CHECK_THROWS(sweepRegions(start, {circleRegion(.5 * scale)}, {{h, h->domain()}}));
        }
    // Riproduttore originale: quattro giri e volume maggiore del box contenitore.
    HelixSpec spec;
    spec.radius = 1;
    spec.pitch = .1;
    spec.turns = 4;
    const auto h = std::make_shared<HelixCurve>(spec);
    const Frame3 start(h->point(0), h->derivative(0), Vec3(1, 0, 0));
    FK_CHECK_THROWS(sweepRegions(start, {circleRegion(.5)}, {{h, h->domain()}}));
    // Dividere il percorso non deve occultare l'incrocio.
    const double mid = .5 * h->domain().hi;
    FK_CHECK_THROWS(sweepRegions(start, {circleRegion(.5)}, {{h, {0, mid}}, {h, {mid, h->domain().hi}}}));
    // Il foro non rimuove l'intersezione fra gli anelli delle spire.
    FK_CHECK_THROWS(sweepRegions(start, {circleRegion(.5, .2)}, {{h, h->domain()}}));
}

FK_TEST(SweepRejectsSingleFaceAndCapSideOverlap) {
    HelixSpec spec;
    spec.radius = 1;
    spec.pitch = .1;
    spec.turns = 2;
    const auto h = std::make_shared<HelixCurve>(spec);
    const Frame3 start(h->point(0), h->derivative(0), Vec3(1, 0, 0));
    // Unica faccia laterale: due parti lontane dello stesso ribbon si
    // attraversano.
    ProfileLoop ribbon;
    ribbon.segments = {lineSegment(Vec2(0, -.5), Vec2(0, .5))};
    FK_CHECK_THROWS(sweepChains(start, {ribbon}, {{h, h->domain()}}, SweepOrientation::Frenet));
    const auto circle = std::make_shared<Circle<3>>(Vec3(), Vec3(1, 0, 0), Vec3(0, 1, 0), 3);
    const Frame3 section(circle->point(0), circle->derivative(0), Vec3(1, 0, 0));
    // Un arco quasi chiuso resta valido: la coordinata angolare e' unica,
    // anche quando i centri delle due teste sono piu' vicini del diametro.
    const auto closeCaps = sweepRegions(section, {circleRegion(.5)}, {{circle, {0, kTwoPi - .1}}});
    FK_CHECK(checkBody(closeCaps).empty());
    FK_CHECK_NEAR(massProperties(closeCaps).volume, kPi * .25 * 3 * (kTwoPi - .1), 1e-8);
    // Elica aperta: il coperchio finale interseca il lato del giro precedente.
    // Il testimone sul carrier effettivo ha capRadius=.400013<.5.
    HelixSpec endSpec;
    endSpec.radius = 1;
    endSpec.pitch = .1;
    endSpec.turns = 1.125;
    const auto endPath = std::make_shared<HelixCurve>(endSpec);
    const Frame3 endSection(endPath->point(0), endPath->derivative(0), Vec3(1, 0, 0));
    FK_CHECK_THROWS(
        sweepRegions(endSection, {circleRegion(.5)}, {{endPath, endPath->domain()}}, SweepOrientation::Frenet));
    const auto valid = sweepRegions(section, {circleRegion(.1)}, {{circle, {0, kTwoPi - .1}}});
    FK_CHECK(checkBody(valid).empty());
    FK_CHECK_NEAR(massProperties(valid).volume, kPi * .01 * 3 * (kTwoPi - .1), 1e-9);
}

FK_TEST(SweepChecksDifferentRegionsAcrossTurns) {
    HelixSpec spec;
    spec.radius = 4;
    spec.pitch = .5;
    spec.turns = 2;
    const auto h = std::make_shared<HelixCurve>(spec);
    const Frame3 start(h->point(0), h->derivative(0), Vec3(1, 0, 0));
    const auto low = k2Disk(Vec2(0, -.25), .1), high = k2Disk(Vec2(0, .25), .1);
    // Ogni tubo isolato e' valido; solo le due regioni assieme si attraversano.
    for (const auto &p : {low, high}) {
        const auto body = sweepRegions(start, {p}, {{h, h->domain()}}, SweepOrientation::Frenet);
        FK_CHECK(checkBody(body).empty());
        FK_CHECK(massProperties(body).volume > 0);
    }
    FK_CHECK_THROWS(sweepRegions(start, {low, high}, {{h, h->domain()}}, SweepOrientation::Frenet));
    const auto separated = sweepRegions(start, {k2Disk(Vec2(-.3, 0), .1), k2Disk(Vec2(.3, 0), .1)}, {{h, h->domain()}},
                                        SweepOrientation::Frenet);
    FK_CHECK(checkBody(separated).empty());
    FK_CHECK_NEAR(massProperties(separated).volume, 2 * kPi * .01 * h->length(), 1e-7);
}

FK_TEST(SweepGlobalCheckPreservesClosedSeamsAndRepresentations) {
    const auto circle = std::make_shared<Circle<3>>(Vec3(), Vec3(1, 0, 0), Vec3(0, 1, 0), 3);
    const Frame3 start(circle->point(0), circle->derivative(0), Vec3(1, 0, 0));
    const auto nurbs = std::make_shared<BSplineCurve<3>>(toBSpline(*circle, 0, kTwoPi));
    for (int representation = 0; representation < 2; ++representation) {
        const CurvePtr<3> path = representation ? std::static_pointer_cast<const Curve<3>>(nurbs) : circle;
        for (int split = 0; split < 2; ++split) {
            std::vector<PathSegment> pieces;
            if (split)
                pieces = {{path, {0, kPi}}, {path, {kPi, kTwoPi}}};
            else
                pieces = {{path, {0, kTwoPi}}};
            const auto body = sweepRegions(start, {circleRegion(.5, .2)}, pieces);
            FK_CHECK(checkBody(body).empty());
            FK_CHECK_NEAR(massProperties(body).volume, 2 * kPi * kPi * 3 * (.25 - .04), 1e-7);
        }
    }
}

FK_TEST(SweepGlobalCheckPreservesSeparatedInterleavedRegions) {
    HelixSpec spec;
    spec.radius = 4;
    spec.pitch = .5;
    spec.turns = 2;
    const auto path = std::make_shared<HelixCurve>(spec);
    const Frame3 start(path->point(0), path->derivative(0), Vec3(1, 0, 0));
    // Le due famiglie restano separate: distanza fra curvecentro >.2495,
    // somma raggi=.2. L'inviluppo convesso comune invece si sovrappone.
    const auto body = sweepRegions(start, {k2Disk(Vec2(0, -.375), .1), k2Disk(Vec2(0, .375), .1)},
                                   {{path, path->domain()}}, SweepOrientation::Frenet);
    FK_CHECK(checkBody(body).empty());
    FK_CHECK_NEAR(massProperties(body).volume, 2 * kPi * .01 * path->length(), 1e-7);
}

FK_TEST(SweepGlobalCheckPreservesRankTwoSheet) {
    ProfileLoop profile;
    profile.segments = {{std::make_shared<BSplineCurve<2>>(2, std::vector<double>{0, 0, 0, 1, 1, 1},
                                                           std::vector<Vec2>{{-1, 0}, {0, .2}, {1, 0}}),
                         {0, 1}}};
    const auto body = sweepChains(Frame3(Vec3(), Vec3(0, 1, 0), Vec3(1, 0, 0)), {profile},
                                  {{std::make_shared<Line<3>>(Vec3(), Vec3(0, 0, 1)), {0, 2}}});
    FK_CHECK(body.isSheet());
    FK_CHECK(checkBody(body).empty());
    double area = 0;
    for (auto face : body.faces())
        area += faceArea(body, face);
    FK_CHECK_NEAR(area, 4, 1e-10);
}

FK_TEST(SweepGlobalCheckRejectsDuplicateMaterial) {
    const auto path = std::make_shared<Line<3>>(Vec3(), Vec3(0, 0, 1));
    const Frame3 frame;
    const std::vector<PathSegment> segments{{path, {0, 2}}};
    FK_CHECK_THROWS(sweepRegions(frame, {k2Disk(Vec2(), 2), k2Disk(Vec2(), .5)}, segments));
    FK_CHECK_THROWS(sweepRegions(frame, {k2Disk(Vec2(), 1), k2Disk(Vec2(), 1)}, segments));
    auto invalid = k2Disk(Vec2(), 2);
    invalid.holes = {reversed(ProfileLoop{{arcSegment(Vec2(3, 0), .2, 0, kTwoPi)}})};
    FK_CHECK_THROWS(sweepRegions(frame, {invalid}, segments));
    invalid = k2Disk(Vec2(), 2, 1);
    invalid.holes.push_back(reversed(ProfileLoop{{arcSegment(Vec2(), .5, 0, kTwoPi)}}));
    FK_CHECK_THROWS(sweepRegions(frame, {invalid}, segments));
    // L'isola nel foro e' una regione distinta valida, senza materiale duplicato.
    const auto body = sweepRegions(frame, {k2Disk(Vec2(), 2, 1), k2Disk(Vec2(), .5)}, segments);
    FK_CHECK(checkBody(body).empty());
    FK_CHECK_NEAR(massProperties(body).volume, kPi * (4 - 1 + .25) * 2, 1e-9);
}

FK_TEST(SweepGlobalFailureDoesNotPublishAliases) {
    HelixSpec spec;
    spec.radius = 1;
    spec.pitch = .1;
    spec.turns = 2;
    const auto path = std::make_shared<HelixCurve>(spec);
    const Frame3 start(path->point(0), path->derivative(0), Vec3(1, 0, 0));
    FaceAliases labels{{"stale"}};
    FK_CHECK_THROWS(
        sweepRegions(start, {circleRegion(.5)}, {{path, path->domain()}}, SweepOrientation::Frenet, &labels));
    FK_CHECK(labels.empty());
}
