#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakeRevol.hxx>
#include <GProp_GProps.hxx>
#include <Standard_Failure.hxx>
#include <gp_Ax1.hxx>
#include <cstdio>

#include "fk_body_check.h"
#include "fk_mass.h"
#include "fk_revolve.h"
#include "fk_tessellate.h"
#include "fk_test_profiles.h"

using namespace fktest;

namespace {

ProfileRegion regionOf(const std::vector<ProfileSegment> &segments) {
    const Profile profile = buildProfile(segments, 1e-9);
    FK_CHECK(profile.regions.size() == 1);
    return profile.regions.front();
}

std::vector<ProfileSegment> polygon(const std::vector<Vec2> &points) {
    std::vector<ProfileSegment> segments;
    for (std::size_t i = 0; i < points.size(); ++i) segments.push_back(lineSegment(points[i], points[(i + 1) % points.size()]));
    return segments;
}

// Solido valido, facce attese, volume e area contro BRepPrimAPI_MakeRevol (o i valori esatti).
void checkRevolution(const Frame3 &frame, const std::vector<ProfileSegment> &segments, int faces, double volume, double area,
                     double relative = 1e-10) {
    const ProfileRegion region = regionOf(segments);
    Body body;
    try {
        body = makeRevolution(frame, region);
    } catch (const std::exception &error) {
        reportFailure(__FILE__, __LINE__, error.what());
        return;
    }
    const std::vector<CheckIssue> issues = checkBody(body);
    for (const CheckIssue &issue : issues) reportFailure(__FILE__, __LINE__, describe(issue.code) + ": " + issue.message);
    FK_CHECK(body.counts().faces == faces);
    MassProperties mass;
    try {
        mass = massProperties(body);
    } catch (const std::exception &error) {
        reportFailure(__FILE__, __LINE__, std::string("massProperties: ") + error.what());
        return;
    }
    if (volume > 0.0) {
        FK_CHECK_NEAR(mass.volume, volume, relative * volume);
        FK_CHECK_NEAR(mass.area, area, relative * area);
    }
    // Riferimento OCCT: la faccia del profilo nel piano XZ di frame, ruotata
    // attorno a Z (BRepPrimAPI_MakeRevol rifiuta alcuni profili con un lato
    // sull'asse: li' bastano i valori esatti).
    const Frame3 profilePlane(frame.origin(), -frame.yDir(), frame.xDir());
    GProp_GProps v, s;
    bool occtDone = true;
    try {
        const TopoDS_Shape occt = BRepPrimAPI_MakeRevol(occtFace(region, profilePlane), gp_Ax1(toPnt(frame.origin()), toDir(frame.zDir()))).Shape();
        BRepGProp::VolumeProperties(occt, v, 1e-12);
        BRepGProp::SurfaceProperties(occt, s, 1e-12);
    } catch (const Standard_Failure &) {
        occtDone = false;
        FK_CHECK(volume > 0.0);
    }
    if (occtDone) {
        FK_CHECK_NEAR(mass.volume, v.Mass(), 1e-6 * v.Mass());
        FK_CHECK_NEAR(mass.area, s.Mass(), 1e-6 * s.Mass());
    }
    TessellationOptions options;
    options.deflection = 1e-3;
    FK_CHECK(tessellate(body, options).failedFaces == 0);
}

}

FK_TEST(RevolutionArcCenterAcrossAxis) {
    // Schizzo 4 di Applicatore.prt: l'arco resta a x > 0, ma il
    // suo centro e' a x = -1.7. Non e' un toro di raggio maggiore positivo.
    const double height = std::sqrt(4.0 * 4.0 - 3.65 * 3.65);
    const auto arc = arcSegment(Vec2(-1.7, height), 4.0, -std::acos(3.65 / 4.0), 0.0);
    const std::vector<ProfileSegment> segments{
        lineSegment(Vec2(0, 0), arc.start()), arc,
        lineSegment(arc.end(), Vec2(3, height)),
        lineSegment(Vec2(3, height), Vec2(3, -2)),
        lineSegment(Vec2(3, -2), Vec2(0, -2)),
        lineSegment(Vec2(0, -2), Vec2(0, 0))};
    checkRevolution(Frame3(), segments, 5, 0.0, 0.0);
}

FK_TEST(RevolutionSolids) {
    const Frame3 frame(Vec3(1, -2, 3), Vec3(0.2, 0.1, 1), Vec3(1, 0, 0));
    // Tubo, cilindro pieno (lato sull'asse), cono pieno, tronco di cono con foro.
    checkRevolution(frame, polygon({Vec2(2, 0), Vec2(5, 0), Vec2(5, 3), Vec2(2, 3)}), 4, kPi * 21.0 * 3.0, 2.0 * kPi * 21.0 + 2.0 * kPi * 7.0 * 3.0);
    checkRevolution(frame, polygon({Vec2(0, 0), Vec2(4, 0), Vec2(4, 3), Vec2(0, 3)}), 3, kPi * 48.0, 2.0 * kPi * 16.0 + 2.0 * kPi * 12.0);
    checkRevolution(frame, polygon({Vec2(0, 0), Vec2(4, 0), Vec2(0, 6)}), 2, kPi * 16.0 * 2.0, kPi * 16.0 + kPi * 4.0 * std::hypot(4.0, 6.0));
    checkRevolution(frame, polygon({Vec2(1, 0), Vec2(5, 0), Vec2(3, 4), Vec2(1, 4)}), 4, 0.0, 0.0);
    // Sfera (semicerchio + diametro sull'asse), toro intero, semisfera cava.
    checkRevolution(frame, {arcSegment(Vec2(0, 0), 3.0, -kHalfPi, kHalfPi), lineSegment(Vec2(0, 3), Vec2(0, -3))}, 1,
                    4.0 * kPi * 27.0 / 3.0, 4.0 * kPi * 9.0);
    checkRevolution(frame, {arcSegment(Vec2(6, 1), 2.0, 0.0, kTwoPi)}, 1, 2.0 * kPi * kPi * 6.0 * 4.0, 4.0 * kPi * kPi * 6.0 * 2.0);
    checkRevolution(frame, {arcSegment(Vec2(0, 0), 4.0, 0.0, kHalfPi), lineSegment(Vec2(0, 4), Vec2(0, 3)), reversed(arcSegment(Vec2(0, 0), 3.0, 0.0, kHalfPi)),
                            lineSegment(Vec2(3, 0), Vec2(4, 0))}, 3, 2.0 * kPi * (64.0 - 27.0) / 3.0, 2.0 * kPi * 25.0 + kPi * 7.0);
    // Rettangolo con un angolo arrotondato (una fascia di toro) e un profilo con spline (superficie di rivoluzione).
    std::vector<ProfileSegment> rounded = roundedRectangle(Vec2(2, -1), 4.0, 3.0, 1.0);
    checkRevolution(frame, rounded, 8, 0.0, 0.0);
    std::vector<Vec2> poles{Vec2(5, 0), Vec2(6, 1), Vec2(4, 2), Vec2(5, 3)};
    auto spline = std::make_shared<BSplineCurve<2>>(3, expandKnots({0.0, 1.0}, {4, 4}), poles);
    checkRevolution(frame, {lineSegment(Vec2(1, 0), Vec2(5, 0)), ProfileSegment{spline, spline->domain()}, lineSegment(Vec2(5, 3), Vec2(1, 3)),
                            lineSegment(Vec2(1, 3), Vec2(1, 0))}, 4, 0.0, 0.0);
}

namespace {

// Rivoluzione parziale contro BRepPrimAPI_MakeRevol con lo stesso angolo.
void checkPartialRevolution(const Frame3 &frame, const std::vector<ProfileSegment> &segments, double angle) {
    const ProfileRegion region = regionOf(segments);
    Body body;
    try {
        body = makeRevolution(frame, region, angle);
    } catch (const std::exception &error) {
        reportFailure(__FILE__, __LINE__, std::string("angolo ") + std::to_string(angle) + ": " + error.what());
        return;
    }
    const std::vector<CheckIssue> issues = checkBody(body);
    for (const CheckIssue &issue : issues) reportFailure(__FILE__, __LINE__, describe(issue.code) + ": " + issue.message);
    MassProperties mass;
    try {
        mass = massProperties(body);
    } catch (const std::exception &error) {
        reportFailure(__FILE__, __LINE__, std::string("massProperties: ") + error.what());
        return;
    }
    const Frame3 profilePlane(frame.origin(), -frame.yDir(), frame.xDir());
    const TopoDS_Shape occt = BRepPrimAPI_MakeRevol(occtFace(region, profilePlane), gp_Ax1(toPnt(frame.origin()), toDir(frame.zDir())), angle).Shape();
    GProp_GProps v, s;
    BRepGProp::VolumeProperties(occt, v, 1e-12);
    BRepGProp::SurfaceProperties(occt, s, 1e-12);
    FK_CHECK_NEAR(mass.volume, v.Mass(), 1e-6 * v.Mass());
    FK_CHECK_NEAR(mass.area, s.Mass(), 1e-6 * s.Mass());
    FK_CHECK_NEAR(mass.centroid.x(), v.CentreOfMass().X(), 1e-6);
    FK_CHECK_NEAR(mass.centroid.y(), v.CentreOfMass().Y(), 1e-6);
    FK_CHECK_NEAR(mass.centroid.z(), v.CentreOfMass().Z(), 1e-6);
    TessellationOptions options;
    options.deflection = 1e-3;
    FK_CHECK(tessellate(body, options).failedFaces == 0);
}

}

FK_TEST(RevolutionPartial) {
    const Frame3 frame(Vec3(1, -2, 3), Vec3(0.2, 0.1, 1), Vec3(1, 0, 0));
    const std::vector<ProfileSegment> tube = polygon({Vec2(2, 0), Vec2(5, 0), Vec2(5, 3), Vec2(2, 3)});
    const std::vector<ProfileSegment> post = polygon({Vec2(0, 0), Vec2(4, 0), Vec2(4, 3), Vec2(0, 3)});
    const std::vector<ProfileSegment> ball{arcSegment(Vec2(0, 0), 3.0, -kHalfPi, kHalfPi), lineSegment(Vec2(0, 3), Vec2(0, -3))};
    const std::vector<ProfileSegment> ring{arcSegment(Vec2(6, 1), 2.0, 0.0, kTwoPi)};
    const std::vector<ProfileSegment> cone = polygon({Vec2(0, 0), Vec2(4, 0), Vec2(0, 6)});
    for (double degrees : {90.0, 180.0, 270.0, -120.0}) {
        const double angle = degrees * kPi / 180.0;
        checkPartialRevolution(frame, tube, angle);
        checkPartialRevolution(frame, post, angle);
        checkPartialRevolution(frame, ball, angle);
        checkPartialRevolution(frame, ring, angle);
        checkPartialRevolution(frame, cone, angle);
    }
    checkPartialRevolution(frame, roundedRectangle(Vec2(2, -1), 4.0, 3.0, 1.0), 2.0);
}

FK_TEST(RevolutionPartialClosedClampedSpline) {
    // Cerchio razionale esatto, ma B-spline clamped non periodica: stesso
    // caso topologico di una spline chiusa creata per punti nello schizzo.
    const auto circle = [](double radius, int source) {
        std::vector<Vec2> poles{{4+radius,0},{4+radius,radius},{4,radius},{4-radius,radius},
                               {4-radius,0},{4-radius,-radius},{4,-radius},{4+radius,-radius},{4+radius,0}};
        std::vector<double> weights{1,std::sqrt(0.5),1,std::sqrt(0.5),1,std::sqrt(0.5),1,std::sqrt(0.5),1};
        auto curve = std::make_shared<BSplineCurve<2>>(2,expandKnots({0,1,2,3,4},{3,2,2,2,3}),poles,weights);
        return ProfileSegment{curve,curve->domain(),source};
    };
    const Frame3 frame(Vec3(1,-2,3),Vec3(.2,.1,1),Vec3(1,0,0));
    const auto outer = circle(1,42);
    const ProfileRegion region = regionOf({outer});
    const auto full = makeRevolution(frame,region);
    FK_CHECK(full.counts().faces == 2);
    FK_CHECK_NEAR(massProperties(full).volume,8*kPi*kPi,1e-6);
    FK_CHECK(checkBody(full).empty());
    for (double degrees : {45.,90.,180.,270.,-90.,-270.,360.,-360.}) {
        const double angle = degrees*kPi/180.;
        if (std::abs(degrees) < 360.) checkPartialRevolution(frame,{outer},angle);
        for (bool reverse : {false,true}) {
            auto profile = region;
            if (reverse) profile.outer = reversed(profile.outer);
            RevolutionProvenance provenance;
            const auto body = makeRevolution(frame,profile,angle,&provenance);
            FK_CHECK(checkBody(body).empty());
            bool start=false,end=false; int sides=0;
            for (const auto &origin : provenance.faces) {
                start = start || origin.role == RevolutionFaceRole::Start;
                end = end || origin.role == RevolutionFaceRole::End;
                if (origin.role == RevolutionFaceRole::Side) {
                    ++sides;
                    FK_CHECK(origin.loop == 0 && origin.segment == 0 && origin.source == 42);
                }
            }
            FK_CHECK((std::abs(degrees) == 360. ? !start && !end : start && end) && sides >= 2);
            const auto mass = massProperties(body);
            FK_CHECK_NEAR(mass.volume,8*kPi*kPi*std::abs(degrees)/360.,1e-6);
            if (std::abs(degrees) == 360.) {
                // Il baricentro del toro completo e' esattamente sull'asse;
                // l'integrale OCCT della spline razionale introduce qui 4,6e-6.
                FK_CHECK(distance(mass.centroid,frame.origin()) < 1e-8);
                FK_CHECK_NEAR(mass.area,16*kPi*kPi,1e-6);
                TessellationOptions options;options.deflection=1e-3;
                FK_CHECK(tessellate(body,options).failedFaces==0);
            }
        }
    }
    // Anche il foro e' un singolo meridiano chiuso non periodico.
    const auto hole = circle(.4,43);
    const auto hollow = regionOf({outer,hole});
    RevolutionProvenance origins;
    const auto body = makeRevolution(frame,hollow,kHalfPi,&origins);
    FK_CHECK(checkBody(body).empty());
    bool outerSource=false,holeSource=false;
    for (const auto &origin : origins.faces) if (origin.role == RevolutionFaceRole::Side) {
        FK_CHECK(origin.segment == 0);
        outerSource = outerSource || (origin.loop == 0 && origin.source == 42);
        holeSource = holeSource || (origin.loop == 1 && origin.source == 43);
    }
    FK_CHECK(outerSource && holeSource);
    FK_CHECK_NEAR(massProperties(body).volume,2*kPi*kPi*(1-.16),1e-6);
    TessellationOptions options; options.deflection=1e-3;
    FK_CHECK(tessellate(body,options).failedFaces == 0);
}

FK_TEST(RevolutionPrimitives) {
    const Frame3 frame(Vec3(1, 2, -1), Vec3(0.3, -0.2, 1), Vec3(1, 0, 0));
    const auto check = [](const Body &body, double volume, double area) {
        const std::vector<CheckIssue> issues = checkBody(body);
        for (const CheckIssue &issue : issues) reportFailure(__FILE__, __LINE__, describe(issue.code) + ": " + issue.message);
        const MassProperties mass = massProperties(body);
        FK_CHECK_NEAR(mass.volume, volume, 1e-10 * volume);
        FK_CHECK_NEAR(mass.area, area, 1e-10 * area);
    };
    check(makeSphere(frame, 2.0), 4.0 / 3.0 * kPi * 8.0, 4.0 * kPi * 4.0);
    check(makeCone(frame, 3.0, 0.0, 4.0), kPi * 9.0 * 4.0 / 3.0, kPi * 9.0 + kPi * 3.0 * 5.0);
    check(makeCone(frame, 0.0, 3.0, 4.0), kPi * 9.0 * 4.0 / 3.0, kPi * 9.0 + kPi * 3.0 * 5.0);
    check(makeCone(frame, 3.0, 1.0, 2.0), kPi * 2.0 / 3.0 * (9.0 + 3.0 + 1.0), kPi * 10.0 + kPi * 4.0 * std::sqrt(8.0));
    check(makeTorus(frame, 5.0, 1.5), 2.0 * kPi * kPi * 5.0 * 2.25, 4.0 * kPi * kPi * 5.0 * 1.5);
    FK_CHECK_THROWS(makeTorus(frame, 1.0, 2.0));
    FK_CHECK_THROWS(makeCone(frame, 0.0, 0.0, 1.0));
}

// Lamine di rivoluzione dalle catene aperte: cilindro + calotta che arriva
// sull'asse (polo), cono a parte; aree esatte, anche nei giri parziali.
FK_TEST(RevolutionSheets) {
    const Frame3 frame(Vec3(1, -2, 3), Vec3(0.2, 0.1, 1), Vec3(1, 0, 0));
    const std::vector<ProfileSegment> segments{lineSegment(Vec2(1, 0), Vec2(1, 2)), arcSegment(Vec2(0, 2), 1.0, 0.0, kHalfPi),
                                               lineSegment(Vec2(2, 0), Vec2(3, 1))};
    const Profile profile = buildProfile(segments, 1e-9);
    FK_CHECK(profile.regions.empty());
    FK_CHECK(profile.chains.size() == 2);
    const double fullArea = 6.0 * kPi + 5.0 * kPi * std::sqrt(2.0);
    for (double degrees : {360.0, 90.0, 180.0, -120.0}) {
        const double angle = degrees * kPi / 180.0;
        Body body;
        try {
            body = makeSheetRevolution(frame, profile.chains, angle);
        } catch (const std::exception &error) {
            reportFailure(__FILE__, __LINE__, error.what());
            continue;
        }
        FK_CHECK(body.isSheet());
        for (const CheckIssue &issue : checkBody(body)) reportFailure(__FILE__, __LINE__, describe(issue.code) + ": " + issue.message);
        double area = 0.0;
        for (FaceId f : body.faces()) area += faceArea(body, f);
        const double expected = fullArea * std::min(1.0, std::fabs(angle) / kTwoPi);
        FK_CHECK_NEAR(area, expected, 1e-8 * expected);
        TessellationOptions options;
        options.deflection = 1e-3;
        FK_CHECK(tessellate(body, options).failedFaces == 0);
    }
    // Una catena tutta sull'asse non da' facce.
    const Profile onAxis = buildProfile({lineSegment(Vec2(0, 0), Vec2(0, 2))}, 1e-9);
    FK_CHECK_THROWS(makeSheetRevolution(frame, onAxis.chains, kTwoPi));
}
