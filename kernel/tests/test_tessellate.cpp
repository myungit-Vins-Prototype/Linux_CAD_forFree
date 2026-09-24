#include "fk_boolean.h"
#include "fk_classify.h"
#include "fk_extrude.h"
#include "fk_mass.h"
#include "fk_primitives.h"
#include "fk_revolve.h"
#include "fk_surface_algo.h"
#include "fk_tessellate.h"
#include "fk_test_profiles.h"

using namespace fktest;

namespace {

struct MeshMeasures {
    double area = 0.0, volume = 0.0;
    double deviation = 0.0;     // distanza massima dei baricentri dei triangoli dalla superficie
    int flipped = 0;            // triangoli con la normale opposta a quella della faccia
    int triangles = 0;
};

MeshMeasures measure(const Body &body, const Tessellation &mesh, double deflection) {
    MeshMeasures result;
    for (const FaceMesh &face : mesh.faces) {
        const Surface &surface = *body.face(face.face).surface;
        for (const std::array<int, 3> &t : face.triangles) {
            const Vec3 &a = face.points[std::size_t(t[0])], &b = face.points[std::size_t(t[1])], &c = face.points[std::size_t(t[2])];
            const Vec3 n = cross(b - a, c - a);
            result.area += 0.5 * norm(n);
            result.volume += dot(a, cross(b, c)) / 6.0;
            ++result.triangles;
            // Orientamento: si escludono i triangoli minuscoli e quelli con un
            // vertice in un punto singolare (polo, vertice del cono), dove la
            // parametrizzazione non conserva l'orientamento dei triangoli grandi.
            const Vec3 vertexNormal = face.normals[std::size_t(t[0])] + face.normals[std::size_t(t[1])] + face.normals[std::size_t(t[2])];
            bool singular = false;
            for (int k : t) {
                try {
                    surface.normal(face.parameters[std::size_t(k)][0], face.parameters[std::size_t(k)][1]);
                } catch (const std::exception &) {
                    singular = true;
                }
            }
            if (!singular && norm(n) > 1e-4 * deflection * deflection && dot(n, vertexNormal) < 0.0) ++result.flipped;
            // Scarto dalla superficie su un triangolo ogni tanto (la proiezione costa).
            if (result.triangles % 7 == 0) {
                const SurfaceProjection projection = projectPoint(surface, (a + b + c) / 3.0);
                result.deviation = std::max(result.deviation, projection.distance);
            }
        }
    }
    return result;
}

// Tassellazione con scarto `deflection`: niente facce perse, triangoli
// orientati come le facce e vicini alla superficie, area e volume vicini a
// quelli esatti.
void checkMesh(const Body &body, double deflection) {
    TessellationOptions options;
    options.deflection = deflection;
    options.angle = 0.2;
    const Tessellation mesh = tessellate(body, options);
    FK_CHECK(mesh.failedFaces == 0);
    FK_CHECK(int(mesh.faces.size()) == body.counts().faces);
    FK_CHECK(int(mesh.edges.size()) == body.counts().edges);
    const MeshMeasures measures = measure(body, mesh, deflection);
    const MassProperties exact = massProperties(body);
    FK_CHECK(measures.flipped == 0);
    FK_CHECK(measures.deviation <= 2.0 * deflection);
    FK_CHECK_NEAR(measures.area, exact.area, 0.02 * exact.area);
    FK_CHECK_NEAR(measures.volume, exact.volume, 3.0 * deflection * exact.area);
    // Piu' fine: piu' vicino.
    options.deflection = 0.25 * deflection;
    const MeshMeasures finer = measure(body, tessellate(body, options), options.deflection);
    FK_CHECK(std::fabs(finer.volume - exact.volume) <= std::fabs(measures.volume - exact.volume) + 1e-9 * exact.volume);
    FK_CHECK_NEAR(finer.volume, exact.volume, 0.75 * deflection * exact.area);
}

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

double polygonArea(const std::vector<Vec2> &points, const std::vector<std::array<int, 3>> &triangles, int &clockwise) {
    double area = 0.0;
    clockwise = 0;
    for (const std::array<int, 3> &t : triangles) {
        const double a = 0.5 * cross(points[std::size_t(t[1])] - points[std::size_t(t[0])], points[std::size_t(t[2])] - points[std::size_t(t[0])]);
        if (a < 0.0) ++clockwise;
        area += a;
    }
    return area;
}

}

// Poligoni con fori, anche non convessi e con vertici allineati.
FK_TEST(TessellateTriangulatePolygon) {
    std::vector<Vec2> outer;
    const int n = 40;
    for (int i = 0; i < n; ++i) {
        const double a = kTwoPi * i / n, r = i % 2 ? 10.0 : 6.0;  // stella
        outer.push_back(Vec2(r * std::cos(a), r * std::sin(a)));
    }
    std::vector<std::vector<Vec2>> holes;
    for (int h = 0; h < 3; ++h) {
        std::vector<Vec2> hole;
        const Vec2 center(2.5 * std::cos(kTwoPi * h / 3), 2.5 * std::sin(kTwoPi * h / 3));
        for (int i = 0; i < 12; ++i) {
            const double a = -kTwoPi * i / 12;
            hole.push_back(center + Vec2(1.0 * std::cos(a), 1.0 * std::sin(a)));
        }
        holes.push_back(hole);
    }
    std::vector<Vec2> all = outer;
    double expected = 0.0;
    for (std::size_t i = 0, j = outer.size() - 1; i < outer.size(); j = i++) expected += 0.5 * cross(outer[j], outer[i]);
    for (const std::vector<Vec2> &hole : holes) {
        all.insert(all.end(), hole.begin(), hole.end());
        for (std::size_t i = 0, j = hole.size() - 1; i < hole.size(); j = i++) expected += 0.5 * cross(hole[j], hole[i]);
    }
    const std::vector<std::array<int, 3>> triangles = triangulatePolygon(outer, holes);
    int clockwise = 0;
    FK_CHECK_NEAR(polygonArea(all, triangles, clockwise), expected, 1e-9 * expected);
    FK_CHECK(clockwise == 0);
    FK_CHECK(int(triangles.size()) == int(all.size()) + 2 * int(holes.size()) - 2);

    // Quadrato con punti allineati sui lati.
    std::vector<Vec2> square;
    for (int i = 0; i < 4; ++i) square.push_back(Vec2(i, 0));
    for (int i = 0; i < 4; ++i) square.push_back(Vec2(4, i));
    for (int i = 4; i > 0; --i) square.push_back(Vec2(i, 4));
    for (int i = 4; i > 0; --i) square.push_back(Vec2(0, i));
    FK_CHECK_NEAR(polygonArea(square, triangulatePolygon(square, {}), clockwise), 16.0, 1e-12);
}

FK_TEST(TessellatePrimitives) {
    std::mt19937 rng(71);
    for (int trial = 0; trial < 3; ++trial) {
        const Frame3 frame = randomFrame(rng, 30.0);
        checkMesh(makeBox(frame, uniform(rng, 1, 20), uniform(rng, 1, 20), uniform(rng, 1, 20)), 0.01);
        checkMesh(makeCylinder(frame, uniform(rng, 1, 10), uniform(rng, 1, 20)), 0.01);
    }
    // Profili con archi, fori e spline chiuse.
    std::vector<ProfileSegment> segments = roundedRectangle(Vec2(-10, -6), 20.0, 12.0, 2.5);
    segments.push_back(closedSpline(Vec2(-3, 0), 2.5, true));
    segments.push_back(arcSegment(Vec2(5, 0), 2.0, 0.0, kTwoPi));
    const ProfileRegion region = buildProfile(segments, 1e-6).regions.front();
    checkMesh(makeExtrusion(Frame3(), region, 5.0), 0.005);
    checkMesh(makeExtrusion(Frame3(Vec3(1, 2, 3), Vec3(0.3, 0.2, 1), Vec3(1, 0, 0)), region, -4.0), 0.005);
}

// Superfici con poli, coni, tori e facce senza bordo.
FK_TEST(TessellatePolesAndTori) {
    const Frame3 frame(Vec3(1, 2, 3), Vec3(0.2, -0.3, 1), Vec3(1, 0, 0));
    const double r = 4.0, h = 7.0;
    const Circle<3> base = makeCircle(frame, r);
    checkMesh(capBody(base, std::make_shared<ConicalSurface>(frame, -std::atan(r / h), r)), 0.005);
    checkMesh(capBody(base, std::make_shared<SphericalSurface>(frame, r)), 0.005);
    {
        Body sphere;
        const Body::MvfsResult start = sphere.mvfs(frame.toGlobal(Vec3(r, 0, 0)));
        sphere.face(start.face).surface = std::make_shared<SphericalSurface>(frame, r);
        checkMesh(sphere, 0.005);
    }
    {
        Body torus;
        const Body::MvfsResult start = torus.mvfs(frame.toGlobal(Vec3(9, 0, 0)));
        torus.face(start.face).surface = std::make_shared<ToroidalSurface>(frame, 7.0, 2.0);
        checkMesh(torus, 0.005);
    }
    {  // cilindro con il fianco sostituito dalla meta' esterna del toro (loop avvolti in u su una superficie doppiamente periodica)
        const double R = 6.0, rho = 2.0;
        const Frame3 shifted(frame.origin() - rho * frame.zDir(), frame.zDir(), frame.xDir());
        Body body = makeCylinder(shifted, R, 2.0 * rho);
        for (FaceId f : body.faces())
            if (body.face(f).surface->type() == SurfaceType::Cylinder)
                body.face(f).surface = std::make_shared<ToroidalSurface>(frame, R, rho);
        for (FinId f : body.fins()) body.fin(f).pcurve = nullptr;
        checkMesh(body, 0.005);
    }
}

// Risultati delle booleane: facce tagliate, fori, curve d'intersezione tracciate.
FK_TEST(TessellateBooleans) {
    const Frame3 frame(Vec3(1, -2, 3), Vec3(0.2, 0.1, 1), Vec3(1, 0, 0));
    auto local = [&](double x, double y, double z) { return frame.toGlobal(Vec3(x, y, z)); };
    const Body block = makeBox(Frame3(local(-8, -8, -6), frame.zDir(), frame.xDir()), 16, 16, 12);
    const Body holeZ = makeCylinder(Frame3(local(0, 0, -10), frame.zDir(), frame.xDir()), 4, 20);
    const Body holeX = makeCylinder(Frame3(local(-10, 0, 1), frame.xDir(), frame.yDir()), 3, 20);
    const Body drilled = booleanOperation(booleanOperation(block, holeZ, BooleanOperation::Subtract), holeX, BooleanOperation::Subtract);
    checkMesh(drilled, 0.01);
    const Body a = makeCylinder(Frame3(local(0, 0, -10), frame.zDir(), frame.xDir()), 5, 20);
    const Body tee = makeCylinder(Frame3(local(0, 0, 2), frame.xDir(), frame.yDir()), 2, 12);
    checkMesh(booleanOperation(a, tee, BooleanOperation::Unite), 0.01);
    checkMesh(booleanOperation(a, tee, BooleanOperation::Intersect), 0.01);
    const Body same = makeCylinder(Frame3(local(-10, 0, 0), frame.xDir(), frame.yDir()), 5, 20);
    checkMesh(booleanOperation(a, same, BooleanOperation::Intersect), 0.01);  // Steinmetz: rami che si incrociano
    // Sfera forata e toro tagliato da un piano per l'asse (loop avvolti in v).
    const Body sphere = makeRevolution(frame, buildProfile({arcSegment(Vec2(0, 0), 4.0, -kHalfPi, kHalfPi), lineSegment(Vec2(0, 4), Vec2(0, -4))}, 1e-9)
                                                  .regions.front());
    checkMesh(booleanOperation(sphere, makeCylinder(Frame3(local(0.5, 0.3, -8), frame.zDir(), frame.xDir()), 1.5, 16), BooleanOperation::Subtract), 0.01);
    const Body torus = makeRevolution(frame, buildProfile({arcSegment(Vec2(5, 0), 1.5, 0.0, kTwoPi)}, 1e-9).regions.front());
    checkMesh(booleanOperation(torus, makeBox(Frame3(local(0, -10, -10), frame.zDir(), frame.xDir()), 20, 20, 20), BooleanOperation::Intersect), 0.01);
}

// Selezione: primo punto colpito da un raggio.
FK_TEST(TessellateRayHits) {
    const Body box = makeBox(Frame3(), 10, 6, 4);
    double t = 0.0;
    FK_CHECK(firstRayHit(box, Vec3(5, 3, 20), Vec3(0, 0, -2), 1e-7, t));
    FK_CHECK_NEAR(t, 8.0, 1e-12);  // direzione non unitaria: t in unita' della direzione
    FK_CHECK(!firstRayHit(box, Vec3(15, 3, 20), Vec3(0, 0, -1), 1e-7, t));
    FK_CHECK(!firstRayHit(box, Vec3(5, 3, 20), Vec3(0, 0, 1), 1e-7, t));
    const Body cylinder = makeCylinder(Frame3(), 3, 5);
    FK_CHECK(firstRayHit(cylinder, Vec3(-10, 0, 2), Vec3(1, 0, 0), 1e-7, t));
    FK_CHECK_NEAR(t, 7.0, 1e-12);
    // Dal foro di un blocco forato si vede il fondo del foro... che non c'e': si passa.
    const Body drilled = booleanOperation(box, makeCylinder(Frame3(Vec3(5, 3, -1), Vec3(0, 0, 1), Vec3(1, 0, 0)), 1.5, 6),
                                          BooleanOperation::Subtract);
    FK_CHECK(!firstRayHit(drilled, Vec3(5, 3, 20), Vec3(0, 0, -1), 1e-7, t));
    FK_CHECK(firstRayHit(drilled, Vec3(5, 3, 2), Vec3(1, 0, 0), 1e-7, t));
    FK_CHECK_NEAR(t, 1.5, 1e-9);
}
