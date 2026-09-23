#include <BRepAdaptor_Curve.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <GCPnts_AbscissaPoint.hxx>
#include <GProp_GProps.hxx>
#include <TopExp_Explorer.hxx>
#include <gp_Mat.hxx>

#include "fk_body_check.h"
#include "fk_extrude.h"
#include "fk_mass.h"
#include "fk_pcurve.h"
#include "fk_primitives.h"
#include "fk_quadrature.h"
#include "fk_test_profiles.h"

using namespace fktest;

namespace {

using Matrix = std::array<std::array<double, 3>, 3>;

// Tensore d'inerzia locale (assi del sistema) portato negli assi globali: R I R^T.
Matrix toGlobal(const Frame3 &frame, const Matrix &local) {
    const Vec3 axes[3] = {frame.xDir(), frame.yDir(), frame.zDir()};
    Matrix result{};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            for (int a = 0; a < 3; ++a)
                for (int b = 0; b < 3; ++b) result[i][j] += axes[a][i] * local[a][b] * axes[b][j];
    return result;
}

// Tensore d'inerzia da momenti secondi centrali M: I = tr(M) Id - M.
Matrix inertiaFromMoments(const Matrix &m) {
    const double trace = m[0][0] + m[1][1] + m[2][2];
    Matrix result{};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) result[i][j] = (i == j ? trace : 0.0) - m[i][j];
    return result;
}

Matrix fromOcct(const gp_Mat &m) {
    Matrix result{};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) result[i][j] = m.Value(i + 1, j + 1);
    return result;
}

struct Expected {
    double volume, area;
    Vec3 centroid;
    Matrix inertia;
};

void checkProperties(const MassProperties &actual, const Expected &expected, double tolerance) {
    FK_CHECK_NEAR(actual.volume, expected.volume, tolerance * expected.volume);
    FK_CHECK_NEAR(actual.area, expected.area, tolerance * expected.area);
    const double size = std::cbrt(expected.volume);
    FK_CHECK_NEAR(distance(actual.centroid, expected.centroid), 0.0, tolerance * std::max(size, norm(expected.centroid)));
    double scale = 0.0;
    for (const auto &row : expected.inertia)
        for (double value : row) scale = std::max(scale, std::fabs(value));
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) FK_CHECK_NEAR(actual.inertia[i][j], expected.inertia[i][j], tolerance * scale);
}

// Momenti della regione piana con il teorema di Green lungo i tratti:
// A = \oint x dy, \int x = \oint x^2/2 dy, \int y = -\oint y^2/2 dx,
// \int x^2 = \oint x^3/3 dy, \int y^2 = -\oint y^3/3 dx, \int xy = \oint x^2 y/2 dy.
std::array<double, 6> greenMoments(const ProfileRegion &region) {
    std::array<double, 6> total{};
    auto addLoop = [&](const ProfileLoop &loop) {
        for (const ProfileSegment &segment : loop.segments) {
            const std::vector<double> breaks = segment.curve->breakpoints(segment.range);
            for (int k = 0; k < 6; ++k)
                for (std::size_t i = 0; i + 1 < breaks.size(); ++i)
                    total[k] += detail::integrate([&](double t) {
                        Vec2 d[2];
                        segment.curve->evaluate(t, 1, d);
                        const double x = d[0].x(), y = d[0].y(), dx = d[1].x(), dy = d[1].y();
                        switch (k) {
                        case 0: return x * dy;
                        case 1: return x * x / 2 * dy;
                        case 2: return -y * y / 2 * dx;
                        case 3: return x * x * x / 3 * dy;
                        case 4: return -y * y * y / 3 * dx;
                        default: return x * x * y / 2 * dy;
                        }
                    }, breaks[i], breaks[i + 1], 1e-15);
        }
    };
    addLoop(region.outer);
    for (const ProfileLoop &hole : region.holes) addLoop(hole);
    return total;
}

// Prisma retto su una regione piana: momenti della regione con il teorema di
// Green (integrali lungo le curve, indipendenti dall'integrazione sulle
// facce) e direzione di estrusione in forma chiusa; perimetro da OCCT.
// Niente BRepGProp::SurfaceProperties per i momenti secondi: sulla faccia con
// un foro a spline, anche con Eps = 1e-13, sbaglia I_xx di 3e-6 (relativo) e
// il baricentro di 1e-8, mentre l'area torna.
Expected prismReference(const ProfileRegion &region, const Frame3 &frame, double height) {
    const std::array<double, 6> g = greenMoments(region);
    const double area = g[0], cx = g[1] / area, cy = g[2] / area;
    const double h = std::fabs(height);
    Matrix m{};  // momenti secondi centrali del prisma, assi locali
    m[0][0] = (g[3] - area * cx * cx) * h;
    m[1][1] = (g[4] - area * cy * cy) * h;
    m[0][1] = m[1][0] = (g[5] - area * cx * cy) * h;
    m[2][2] = area * h * h * h / 12.0;

    const TopoDS_Face base = occtFace(region, Frame3());
    GProp_GProps properties;
    BRepGProp::SurfaceProperties(base, properties, 1e-13);
    FK_CHECK_NEAR(std::fabs(properties.Mass()), area, 1e-11 * area);
    double perimeter = 0.0;
    for (TopExp_Explorer edges(base, TopAbs_EDGE); edges.More(); edges.Next()) {
        BRepAdaptor_Curve curve(TopoDS::Edge(edges.Current()));
        perimeter += GCPnts_AbscissaPoint::Length(curve, 1e-12);
    }
    return {area * h, 2.0 * area + perimeter * h, frame.toGlobal(Vec3(cx, cy, 0.5 * height)),
            toGlobal(frame, inertiaFromMoments(m))};
}

// Dopo aver cambiato la geometria le SP-curve vanno ricalcolate.
void resetPCurves(Body &body) {
    for (FinId f : body.fins()) body.fin(f).pcurve = nullptr;
    FK_CHECK(computePCurves(body) == 0);
}

bool valid(const Body &body) {
    const std::vector<CheckIssue> issues = checkBody(body);
    for (const CheckIssue &issue : issues) reportFailure(__FILE__, __LINE__, describe(issue.code) + ": " + issue.message);
    return issues.empty();
}

// Solido con due facce e un solo edge chiuso (mvfs + mef): la faccia della
// fin forward (il cerchio percorso nel suo verso) prende `forwardSurface`,
// l'altra il piano del cerchio con la normale opposta all'asse. Senza
// SP-curve: qui le proprieta' di massa passano per l'inversione della superficie.
Body capBody(const Circle<3> &circle, const SurfacePtr &forwardSurface) {
    Body body;
    const Body::MvfsResult start = body.mvfs(circle.point(0.0));
    const Body::MefResult closing = body.mef(start.loop);
    Edge &edge = body.edge(closing.edge);
    edge.curve = std::make_shared<Circle<3>>(circle);
    edge.range = {0.0, kTwoPi};
    const Vec3 axis = cross(circle.xAxis(), circle.yAxis());
    const FaceId forward = body.finFace(edge.forward), backward = body.finFace(edge.backward);
    body.face(forward).surface = forwardSurface;
    body.face(backward).surface = std::make_shared<Plane>(Frame3(circle.center(), -axis, circle.xAxis()));
    return body;
}

}

// Parallelepipedi in posizione e orientamento casuali: tutto in forma chiusa.
FK_TEST(MassOfBoxes) {
    std::mt19937 rng(200);
    for (int trial = 0; trial < 20; ++trial) {
        const Frame3 frame = randomFrame(rng, 200.0);
        const double a = uniform(rng, 0.1, 50.0), b = uniform(rng, 0.1, 50.0), c = uniform(rng, 0.1, 50.0);
        const Body body = makeBox(frame, a, b, c);
        const double volume = a * b * c;
        Matrix local{};
        local[0][0] = volume * (b * b + c * c) / 12.0;
        local[1][1] = volume * (a * a + c * c) / 12.0;
        local[2][2] = volume * (a * a + b * b) / 12.0;
        checkProperties(massProperties(body), {volume, 2.0 * (a * b + b * c + a * c), frame.toGlobal(Vec3(a, b, c) * 0.5),
                                               toGlobal(frame, local)}, 1e-11);
    }
}

// Cilindri (fianco senza cucitura, due loop), anche con altezza negativa.
FK_TEST(MassOfCylinders) {
    std::mt19937 rng(201);
    for (int trial = 0; trial < 20; ++trial) {
        const Frame3 frame = randomFrame(rng, 200.0);
        const double r = uniform(rng, 0.1, 30.0), h = uniform(rng, 0.1, 60.0) * (trial % 2 ? -1.0 : 1.0);
        const Body body = makeCylinder(frame, r, h);
        const double volume = kPi * r * r * std::fabs(h);
        Matrix local{};
        local[0][0] = local[1][1] = volume * (3.0 * r * r + h * h) / 12.0;
        local[2][2] = volume * r * r / 2.0;
        checkProperties(massProperties(body), {volume, kTwoPi * r * (r + std::fabs(h)), frame.toGlobal(Vec3(0, 0, 0.5 * h)),
                                               toGlobal(frame, local)}, 1e-11);
    }
}

// Prismi poligonali con fori contro BRepGProp::VolumeProperties (esatto sulle
// facce piane).
FK_TEST(MassOfPrismsMatchesOcct) {
    std::mt19937 rng(202);
    for (int trial = 0; trial < 10; ++trial) {
        const Frame3 frame = randomFrame(rng, 100.0);
        std::vector<Vec2> outer;
        for (int i = 0; i < 14; ++i) {
            const double angle = kTwoPi * i / 14.0, radius = (i % 2 ? 6.0 : 12.0) * uniform(rng, 0.9, 1.1);
            outer.push_back(Vec2(radius * std::cos(angle), radius * std::sin(angle)));
        }
        const std::vector<std::vector<Vec2>> holes{{Vec2(-3, -1), Vec2(-1, -1), Vec2(-1, 1), Vec2(-3, 1)},
                                                   {Vec2(1, -1), Vec2(3, -2), Vec2(2.5, 1.5)}};
        const double height = uniform(rng, 1.0, 20.0);
        const Body body = makePrism(frame, outer, holes, height);
        const MassProperties actual = massProperties(body);

        ProfileRegion region;
        auto loop = [](const std::vector<Vec2> &points) {
            ProfileLoop result;
            for (std::size_t i = 0; i < points.size(); ++i) result.segments.push_back(lineSegment(points[i], points[(i + 1) % points.size()]));
            return result;
        };
        region.outer = loop(outer);
        for (const std::vector<Vec2> &hole : holes) region.holes.push_back(reversed(loop(hole)));
        const TopoDS_Shape reference = BRepPrimAPI_MakePrism(occtFace(region, frame), gp_Vec(toPnt(Vec3()), toPnt(height * frame.zDir()))).Shape();
        GProp_GProps volume, surface;
        BRepGProp::VolumeProperties(reference, volume, 1e-12);
        BRepGProp::SurfaceProperties(reference, surface, 1e-12);
        checkProperties(actual, {volume.Mass(), surface.Mass(), fromOcct(volume.CentreOfMass()), fromOcct(volume.MatrixOfInertia())},
                        1e-9);
    }
}

// Estrusioni di profili con archi, ellissi, spline e NURBS (fianchi
// cilindrici ed estrusi) contro il riferimento composto (prismReference).
FK_TEST(MassOfExtrusions) {
    std::mt19937 rng(203);
    for (int trial = 0; trial < 12; ++trial) {
        const Frame3 frame = randomFrame(rng, 50.0);
        const double height = uniform(rng, 0.5, 30.0) * (trial % 3 == 0 ? -1.0 : 1.0);
        std::vector<ProfileSegment> segments;
        switch (trial % 4) {
        case 0:
            segments = roundedRectangle(Vec2(-10, -6), 20.0, 12.0, 2.5);
            segments.push_back(arcSegment(Vec2(-4, 0), 2.0, 0.0, kTwoPi));
            segments.push_back(closedSpline(Vec2(4, 0), 2.0, false));
            break;
        case 1:
            segments = {closedSpline(Vec2(), 8.0, true),
                        {std::make_shared<Ellipse<2>>(makeEllipse(Vec2(0.5, 0.3), 2.0, 1.0)), {0.0, kTwoPi}}};
            break;
        case 2: {
            auto spline = std::make_shared<BSplineCurve<2>>(
                3, std::vector<double>{0, 0, 0, 0, 0.5, 1, 1, 1, 1},
                std::vector<Vec2>{Vec2(-5, 0), Vec2(-4, 4), Vec2(0, 6), Vec2(4, 2), Vec2(5, 0)});
            segments = {{spline, spline->domain()}, lineSegment(Vec2(5, 0), Vec2(0, -3)), lineSegment(Vec2(0, -3), Vec2(-5, 0))};
            break;
        }
        default:
            segments = {lineSegment(Vec2(0, -3), Vec2(0, 3)), arcSegment(Vec2(), 3.0, kHalfPi, 1.5 * kPi)};
            break;
        }
        const Profile profile = buildProfile(segments, 1e-6);
        FK_CHECK(profile.regions.size() == 1);
        if (profile.regions.size() != 1) continue;
        const ProfileRegion &region = profile.regions.front();
        const Body body = makeExtrusion(frame, region, height);
        checkProperties(massProperties(body), prismReference(region, frame, height), 1e-11);
    }
}

// Tronco di cono: il cilindro di makeCylinder con il cerchio superiore
// ristretto e il fianco conico. Confronto con la forma chiusa e con OCCT.
FK_TEST(MassOfConeFrustum) {
    std::mt19937 rng(204);
    for (int trial = 0; trial < 8; ++trial) {
        const Frame3 frame = randomFrame(rng, 100.0);
        const double r1 = uniform(rng, 1.0, 20.0), r2 = uniform(rng, 0.5, 20.0), h = uniform(rng, 1.0, 40.0);
        Body body = makeCylinder(frame, r1, h);
        for (EdgeId e : body.edges()) {
            Edge &edge = body.edge(e);
            const auto &circle = static_cast<const Circle<3> &>(*edge.curve);
            if (dot(circle.center() - frame.origin(), frame.zDir()) < 0.5 * h) continue;
            edge.curve = std::make_shared<Circle<3>>(circle.center(), circle.xAxis(), circle.yAxis(), r2);
            body.vertex(body.edgeStart(e)).point = edge.curve->point(edge.range.lo);
        }
        for (FaceId f : body.faces())
            if (body.face(f).surface->type() == SurfaceType::Cylinder)
                body.face(f).surface = std::make_shared<ConicalSurface>(frame, std::atan((r2 - r1) / h), r1);
        resetPCurves(body);
        FK_CHECK(valid(body));

        const double volume = kPi * h * (r1 * r1 + r1 * r2 + r2 * r2) / 3.0;
        const double zc = h * (r1 * r1 + 2.0 * r1 * r2 + 3.0 * r2 * r2) / (4.0 * (r1 * r1 + r1 * r2 + r2 * r2));
        const double slant = std::hypot(h, r2 - r1);
        const TopoDS_Shape cone = BRepPrimAPI_MakeCone(gp_Ax2(toPnt(frame.origin()), toDir(frame.zDir()), toDir(frame.xDir())), r1, r2, h).Shape();
        GProp_GProps properties;
        BRepGProp::VolumeProperties(cone, properties, 1e-12);
        const MassProperties actual = massProperties(body);
        checkProperties(actual, {volume, kPi * (r1 * r1 + r2 * r2 + (r1 + r2) * slant), frame.toGlobal(Vec3(0, 0, zc)),
                                 fromOcct(properties.MatrixOfInertia())}, 1e-9);
        FK_CHECK_NEAR(properties.Mass(), volume, 1e-9 * volume);
    }
}

// Facce che contengono un polo: cono fino al vertice, semisfera, sfera
// intera (faccia senza loop), toro intero e mezzo toro (fascia sul toro).
FK_TEST(MassWithPolesAndTori) {
    std::mt19937 rng(205);
    for (int trial = 0; trial < 6; ++trial) {
        const Frame3 frame = randomFrame(rng, 50.0);
        const double r = uniform(rng, 1.0, 15.0), h = uniform(rng, 1.0, 30.0);
        const Circle<3> base = makeCircle(frame, r);

        {  // cono pieno: la faccia conica contiene il vertice
            const Body cone = capBody(base, std::make_shared<ConicalSurface>(frame, -std::atan(r / h), r));
            FK_CHECK(valid(cone));
            const double volume = kPi * r * r * h / 3.0;
            Matrix local{};
            local[0][0] = local[1][1] = volume * (3.0 * r * r / 20.0 + 3.0 * h * h / 80.0);
            local[2][2] = volume * 3.0 * r * r / 10.0;
            checkProperties(massProperties(cone), {volume, kPi * r * (r + std::hypot(r, h)), frame.toGlobal(Vec3(0, 0, 0.25 * h)),
                                                   toGlobal(frame, local)}, 1e-11);
        }
        {  // semisfera: la faccia sferica contiene il polo nord
            const Body hemisphere = capBody(base, std::make_shared<SphericalSurface>(frame, r));
            FK_CHECK(valid(hemisphere));
            const double volume = 2.0 * kPi * r * r * r / 3.0;
            Matrix local{};
            local[0][0] = local[1][1] = volume * 83.0 * r * r / 320.0;
            local[2][2] = volume * 2.0 * r * r / 5.0;
            checkProperties(massProperties(hemisphere), {volume, 3.0 * kPi * r * r, frame.toGlobal(Vec3(0, 0, 3.0 * r / 8.0)),
                                                         toGlobal(frame, local)}, 1e-11);
        }
        {  // sfera intera: una faccia senza edge (loop di un vertice isolato)
            Body sphere;
            const Body::MvfsResult start = sphere.mvfs(frame.toGlobal(Vec3(r, 0, 0)));
            sphere.face(start.face).surface = std::make_shared<SphericalSurface>(frame, r);
            const double volume = 4.0 * kPi * r * r * r / 3.0;
            Matrix local{};
            local[0][0] = local[1][1] = local[2][2] = volume * 2.0 * r * r / 5.0;
            checkProperties(massProperties(sphere), {volume, 4.0 * kPi * r * r, frame.origin(), toGlobal(frame, local)}, 1e-11);
        }
        const double R = r + uniform(rng, 0.5, 10.0), rho = uniform(rng, 0.2, 1.0) * r;
        {  // toro intero
            Body torus;
            const Body::MvfsResult start = torus.mvfs(frame.toGlobal(Vec3(R + rho, 0, 0)));
            torus.face(start.face).surface = std::make_shared<ToroidalSurface>(frame, R, rho);
            const double volume = 2.0 * kPi * kPi * R * rho * rho;
            Matrix local{};
            local[0][0] = local[1][1] = volume * (4.0 * R * R + 5.0 * rho * rho) / 8.0;
            local[2][2] = volume * (4.0 * R * R + 3.0 * rho * rho) / 4.0;
            checkProperties(massProperties(torus), {volume, 4.0 * kPi * kPi * R * rho, frame.origin(), toGlobal(frame, local)}, 1e-11);
        }
        {  // cilindro di raggio R e altezza 2 rho con il fianco sostituito dalla meta' esterna del toro
            const Frame3 shifted(frame.origin() - rho * frame.zDir(), frame.zDir(), frame.xDir());
            Body body = makeCylinder(shifted, R, 2.0 * rho);
            for (FaceId f : body.faces())
                if (body.face(f).surface->type() == SurfaceType::Cylinder)
                    body.face(f).surface = std::make_shared<ToroidalSurface>(frame, R, rho);
            resetPCurves(body);
            FK_CHECK(valid(body));
            // Volume: cilindro + meta' esterna del toro (Pappo: semicerchio con baricentro a R + 4 rho / 3 pi).
            const double volume = 2.0 * kPi * R * R * rho + kPi * kPi * R * rho * rho + 4.0 * kPi * rho * rho * rho / 3.0;
            const double area = 2.0 * kPi * R * R + 2.0 * kPi * rho * (kPi * R + 2.0 * rho);
            const MassProperties actual = massProperties(body);
            FK_CHECK_NEAR(actual.volume, volume, 1e-9 * volume);
            FK_CHECK_NEAR(actual.area, area, 1e-9 * area);
            FK_CHECK(distance(actual.centroid, frame.origin()) <= 1e-9 * R);
        }
    }
}

FK_TEST(FaceAreas) {
    const Body box = makeBox(Frame3(), 2.0, 3.0, 5.0);
    double total = 0.0;
    for (FaceId f : box.faces()) {
        const double a = faceArea(box, f);
        FK_CHECK(std::fabs(a - 6.0) < 1e-12 || std::fabs(a - 10.0) < 1e-12 || std::fabs(a - 15.0) < 1e-12);
        total += a;
    }
    FK_CHECK_NEAR(total, 62.0, 1e-11);
}

// Vertici tolleranti (estremi dello schizzo uniti entro 1e-6) e casi non
// gestiti, che devono essere segnalati e non dare numeri sbagliati.
FK_TEST(MassEdgeCases) {
    const double gap = 5e-7;
    const std::vector<ProfileSegment> segments = {lineSegment(Vec2(0, 0), Vec2(10, 0)), lineSegment(Vec2(10, gap), Vec2(10, 10)),
                                                  lineSegment(Vec2(10, 10), Vec2(0, 10)), lineSegment(Vec2(0, 10), Vec2(0, 0))};
    const Body tolerant = makeExtrusion(Frame3(), buildProfile(segments, 1e-6).regions.front(), 2.0);
    const MassProperties properties = massProperties(tolerant);
    FK_CHECK_NEAR(properties.volume, 200.0, 1e-5);
    FK_CHECK(distance(properties.centroid, Vec3(5, 5, 1)) < 1e-6);

    Body open;  // piano senza bordo: faccia illimitata
    const Body::MvfsResult start = open.mvfs(Vec3());
    open.face(start.face).surface = std::make_shared<Plane>(Frame3());
    FK_CHECK_THROWS(massProperties(open));
    Body noSurface = makeBox(Frame3(), 1, 1, 1);
    noSurface.face(noSurface.faces().front()).surface = nullptr;
    FK_CHECK_THROWS(massProperties(noSurface));
}
