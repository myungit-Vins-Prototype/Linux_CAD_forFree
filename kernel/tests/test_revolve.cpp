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
