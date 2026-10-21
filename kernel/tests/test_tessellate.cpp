#include <atomic>
#include "fk_body_check.h"
#include "fk_boolean.h"
#include "fk_classify.h"
#include "fk_extrude.h"
#include "fk_mass.h"
#include "fk_primitives.h"
#include "fk_revolve.h"
#include "fk_surface_algo.h"
#include "fk_surface_batch.h"
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
            // vertice in un punto singolare (polo, vertice del cono) o a meno
            // della deflessione da esso, dove la parametrizzazione non
            // conserva l'orientamento dei triangoli grandi.
            const Vec3 vertexNormal = face.normals[std::size_t(t[0])] + face.normals[std::size_t(t[1])] + face.normals[std::size_t(t[2])];
            bool singular = false;
            for (int k : t) {
                try {
                    surface.normal(face.parameters[std::size_t(k)][0], face.parameters[std::size_t(k)][1]);
                } catch (const std::exception &) {
                    singular = true;
                }
            }
            for (int k : t)
                singular = singular || poleIndex(surfacePoles(surface), face.points[std::size_t(k)], deflection) >= 0;
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
    FK_CHECK(finer.flipped == 0);
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

FK_TEST(TessellateSelectedFaces) {
    const Body body = makeBox(Frame3(), 4.0, 3.0, 2.0);
    const FaceId selected = body.faces().front();
    TessellationOptions options;
    options.deflection = 0.01;
    options.faces = {selected};
    const Tessellation mesh = tessellate(body, options);
    FK_CHECK(mesh.failedFaces == 0);
    FK_CHECK(mesh.faces.size() == 1);
    FK_CHECK(mesh.faces.front().face == selected);
    FK_CHECK(mesh.edges.size() == body.loopFins(body.face(selected).loops.front()).size());
}

// La dimensione della mesh STL deve poter essere limitata indipendentemente
// dallo scarto: su un piano la sola deflessione non aggiungerebbe triangoli.
FK_TEST(TessellateMaximumEdgeLength) {
    TessellationOptions coarse;
    coarse.deflection = 1.0;
    coarse.angle = kPi;
    TessellationOptions limited = coarse;
    limited.maxEdgeLength = 1.5;
    for (const Body &body : {makeBox(Frame3(), 12.0, 8.0, 4.0), makeSphere(Frame3(), 5.0), makeTorus(Frame3(), 5.0, 1.5)}) {
        const Tessellation base = tessellate(body, coarse);
        const Tessellation fine = tessellate(body, limited);
        std::size_t baseTriangles = 0, fineTriangles = 0;
        double longest = 0.0;
        for (const FaceMesh &face : base.faces) baseTriangles += face.triangles.size();
        for (const FaceMesh &face : fine.faces) {
            fineTriangles += face.triangles.size();
            for (const std::array<int, 3> &triangle : face.triangles)
                for (int k = 0; k < 3; ++k)
                    longest = std::max(longest, distance(face.points[std::size_t(triangle[std::size_t(k)])],
                                                         face.points[std::size_t(triangle[std::size_t((k + 1) % 3)])]));
        }
        FK_CHECK(fine.failedFaces == 0);
        FK_CHECK(fineTriangles > baseTriangles);
        FK_CHECK(longest <= 1.5 + 1e-9);
    }
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
    // Loop che passano per i poli: mezza sfera, mezzo cono, anse di un cilindro che contiene l'asse.
    const Body half = makeBox(Frame3(local(0, -6, -6), frame.zDir(), frame.xDir()), 10, 12, 12);
    checkMesh(booleanOperation(sphere, half, BooleanOperation::Intersect), 0.01);
    checkMesh(booleanOperation(sphere, half, BooleanOperation::Subtract), 0.01);
    const Body cone = makeRevolution(frame, buildProfile({lineSegment(Vec2(0, -3), Vec2(4, -3)), lineSegment(Vec2(4, -3), Vec2(0, 5)),
                                                          lineSegment(Vec2(0, 5), Vec2(0, -3))}, 1e-9).regions.front());
    checkMesh(booleanOperation(cone, half, BooleanOperation::Intersect), 0.01);
    checkMesh(booleanOperation(cone, makeCylinder(Frame3(local(1.5, 0, -8), frame.zDir(), frame.xDir()), 1.5, 16), BooleanOperation::Subtract), 0.01);
    checkMesh(booleanOperation(sphere, makeCylinder(Frame3(local(1.5, 0, -8), frame.zDir(), frame.xDir()), 1.5, 16), BooleanOperation::Subtract), 0.01);
    // Toro tagliato da un piano bitangente (cerchi di Villarceau: facce con un loop che si tocca).
    const double tilt = std::asin(1.5 / 5.0);
    const Vec3 n = normalized(frame.toGlobal(Vec3(0, -std::sin(tilt), std::cos(tilt))) - frame.origin());
    const Frame3 plane(frame.origin(), n, frame.xDir());
    const Body bitangent = makeBox(Frame3(frame.origin() - 10.0 * plane.xDir() - 10.0 * plane.yDir(), n, plane.xDir()), 20, 20, 10);
    checkMesh(booleanOperation(torus, bitangent, BooleanOperation::Intersect), 0.01);
    // Cupola piatta nel polo e cilindro tangente li' (rami di un contatto di ordine superiore).
    auto meridian = std::make_shared<BSplineCurve<2>>(3, std::vector<double>{0, 0, 0, 0, 1, 1, 1, 1},
                                                      std::vector<Vec2>{Vec2(3, 0), Vec2(3, 4), Vec2(2, 4), Vec2(0, 4)});
    const Body dome = makeRevolution(frame, buildProfile({lineSegment(Vec2(0, 0), Vec2(3, 0)), ProfileSegment{meridian, meridian->domain()},
                                                          lineSegment(Vec2(0, 4), Vec2(0, 0))}, 1e-9).regions.front());
    const Body roller = makeCylinder(Frame3(local(-6, 0, -6), frame.xDir(), frame.yDir()), 10.0, 12.0);
    checkMesh(booleanOperation(dome, roller, BooleanOperation::Intersect), 0.01);
    checkMesh(booleanOperation(dome, roller, BooleanOperation::Subtract), 0.01);
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

FK_TEST(TessellateCachedRayHits) {
    const Body box = makeBox(Frame3(), 10, 6, 4);
    const Body drilled = booleanOperation(box, makeCylinder(Frame3(Vec3(5, 3, -1), Vec3(0, 0, 1), Vec3(1, 0, 0)), 1.5, 6),
                                         BooleanOperation::Subtract);
    const std::vector<Body> bodies = {box, drilled, makeSphere(Frame3(), 4), makeTorus(Frame3(), 4, 1)};
    for (const Body &body : bodies) {
        const RayFaceIndex index(body);
        for (int x = -6; x <= 12; ++x)
            for (int y = -6; y <= 8; ++y) {
                const Vec3 origin(x + 0.13, y + 0.19, 20), direction(0.01, -0.02, -2);
                double expected = 0, cached = 0;
                FaceId expectedFace, cachedFace;
                const bool a = firstRayHit(body, origin, direction, 1e-7, expected, &expectedFace);
                const bool b = firstRayHit(body, origin, direction, 1e-7, cached, &cachedFace, &index);
                FK_CHECK(a == b);
                if (a && b) {
                    FK_CHECK_NEAR(cached, expected, 1e-12);
                    FK_CHECK(cachedFace == expectedFace);
                }
            }
        double t = 0;
        FK_CHECK(!firstRayHit(body, Vec3(1e4, 1e4, 1e4), Vec3(0, 0, 1), 1e-7, t, nullptr, &index));
    }
    const RayFaceIndex index(drilled);
    double t = 0;
    FK_CHECK(!firstRayHit(drilled, Vec3(5, 3, 20), Vec3(0, 0, -1), 1e-7, t, nullptr, &index));
    FK_CHECK(firstRayHit(drilled, Vec3(5, 3, 2), Vec3(1, 0, 0), 1e-7, t, nullptr, &index));
    FK_CHECK_NEAR(t, 1.5, 1e-9);
    const Tessellation mesh = tessellate(drilled, {});
    FK_CHECK(mesh.edges.size() == mesh.edgeIds.size());
    for (std::size_t i = 0; i < mesh.edges.size(); ++i) {
        const Edge &edge = drilled.edge(mesh.edgeIds[i]);
        FK_CHECK(near(mesh.edges[i].front(), edge.curve->point(edge.range.lo), 1e-12));
        FK_CHECK(near(mesh.edges[i].back(), edge.curve->point(edge.range.hi), 1e-12));
    }
}

// Faccia piu' sottile della deflessione: la lunula tra due archi (larga 0.06,
// lunga 5.25, punte a 2.6 gradi) con deflessione 0.1. Con i soli campioni
// della deflessione le corde di un arco attraversano l'altro, il contorno in
// (u, v) si ripiega e i triangoli escono dalla faccia (sugli STEP: la striscia
// tra una tasca e lo spigolo esterno disegnata sopra la tasca).
FK_TEST(TessellateFaceThinnerThanDeflection) {
    const double a = std::atan2(9.6496, 2.625), b = std::atan2(11.7096, 2.625);
    const std::vector<ProfileSegment> segments = {arcSegment(Vec2(0, 0), 10.0, a, kPi - a),
                                                  arcSegment(Vec2(0, -2.06), 12.0, b, kPi - b)};
    const ProfileRegion region = buildProfile(segments, 1e-3).regions.front();
    const Body body = makeExtrusion(Frame3(), region, 1.0);
    FK_CHECK(checkBody(body).empty());
    TessellationOptions options;
    options.deflection = 0.1;
    const Tessellation mesh = tessellate(body, options);
    FK_CHECK(mesh.failedFaces == 0);
    int planes = 0;
    for (const FaceMesh &face : mesh.faces) {
        if (body.face(face.face).surface->type() != SurfaceType::Plane) continue;
        ++planes;
        double area = 0.0;
        int triangles = 0;
        for (const std::array<int, 3> &t : face.triangles) {
            const Vec3 &p = face.points[std::size_t(t[0])], &q = face.points[std::size_t(t[1])], &r = face.points[std::size_t(t[2])];
            const Vec3 n = cross(q - p, r - p);
            area += 0.5 * norm(n);
            FK_CHECK(dot(n, face.normals[std::size_t(t[0])]) > 0.0);
            // Fuori dalla lunula al piu' di una frazione della deflessione.
            const Vec3 c = (p + q + r) / 3.0;
            const double outside = std::max({std::hypot(c[0], c[1]) - 10.0, 12.0 - std::hypot(c[0], c[1] + 2.06), 0.0});
            FK_CHECK(outside <= 0.25 * options.deflection);
            ++triangles;
        }
        // Il contorno e' fatto di corde entro la deflessione: l'area puo'
        // scostarsi (la faccia e' piu' sottile della deflessione), ma i
        // triangoli non si sovrappongono e non coprono altro.
        const double exact = faceArea(body, face.face);
        FK_CHECK(triangles > 0);
        FK_CHECK_NEAR(area, exact, 0.25 * exact);
    }
    FK_CHECK(planes == 2);
}

// Striscia sottile su un cilindro (larga 0.001) tra il taglio obliquo e una
// cava: i loop della faccia hanno le SP-curve in periodi diversi (la cava
// prima o dopo l'inizio dell'ellisse), e le corde si confrontano a meno di
// periodi interi. Prima l'area dei triangoli arrivava al doppio di quella vera.
FK_TEST(TessellateThinBandAcrossPeriods) {
    for (double offset : {-4.0, 4.0}) {
        const Vec3 crest(std::cos(1.0), std::sin(1.0), 0.0);
        const Vec3 n = normalized(Vec3(-0.3 * crest[0], -0.3 * crest[1], 1.0));
        const Frame3 plane(Vec3(0, 0, 5), n, crest);
        const Body cut = booleanOperation(makeCylinder(Frame3(), 10.0, 10.0),
            makeBox(Frame3(Vec3(0, 0, 5) - plane.xDir() * 30.0 - plane.yDir() * 30.0, n, crest), 60, 60, 20), BooleanOperation::Subtract);
        const Body body = booleanOperation(cut,
            makeBox(Frame3(Vec3(0, 0, 5) - n * 1.001 + plane.xDir() * 5.0 - plane.yDir() * (3.0 - offset), n, crest), 10, 6, 1.0),
            BooleanOperation::Subtract);
        FK_CHECK(checkBody(body).empty());
        TessellationOptions options;
        options.deflection = 0.1;
        const Tessellation mesh = tessellate(body, options);
        FK_CHECK(mesh.failedFaces == 0);
        int bands = 0;
        for (const FaceMesh &face : mesh.faces) {
            if (body.face(face.face).surface->type() != SurfaceType::Cylinder || body.face(face.face).loops.size() < 3) continue;
            ++bands;
            double area = 0.0;
            for (const std::array<int, 3> &t : face.triangles)
                area += 0.5 * norm(cross(face.points[std::size_t(t[1])] - face.points[std::size_t(t[0])],
                                         face.points[std::size_t(t[2])] - face.points[std::size_t(t[0])]));
            FK_CHECK_NEAR(area, faceArea(body, face.face), 0.02 * faceArea(body, face.face));
        }
        FK_CHECK(bands == 1);
    }
}

// Facce in parallelo e valutazione a lotti (l'interfaccia dell'acceleratore
// CUDA dell'app): la mesh non dipende dal numero di thread, e un acceleratore
// che da' gli stessi valori della CPU (o che rifiuta le superfici) non la cambia.
FK_TEST(TessellateThreadsAndBatchEvaluator) {
    struct CpuEvaluator final : SurfaceBatchEvaluator {
        bool accept = true;
        std::atomic<std::size_t> points{0}, smallest{~std::size_t(0)};
        bool evaluate(const Surface &surface, const Vec2 *uv, std::size_t count, Vec3 *out, Vec3 *normals) override {
            if (!accept) return false;
            for (std::size_t i = 0; i < count; ++i) {
                out[i] = surface.point(uv[i][0], uv[i][1]);
                try {
                    normals[i] = surface.normal(uv[i][0], uv[i][1]);
                } catch (const std::exception &) {
                    normals[i] = Vec3();
                }
            }
            points += count;
            for (std::size_t s = smallest; count < s && !smallest.compare_exchange_weak(s, count);) {
            }
            return true;
        }
    };
    std::vector<ProfileSegment> segments = roundedRectangle(Vec2(-10, -6), 20.0, 12.0, 2.5);
    segments.push_back(closedSpline(Vec2(-3, 0), 2.5, true));
    segments.push_back(arcSegment(Vec2(5, 0), 2.0, 0.0, kTwoPi));
    const Body body = makeExtrusion(Frame3(), buildProfile(segments, 1e-6).regions.front(), 5.0);
    const Body sphere = makeSphere(Frame3(Vec3(1, 2, 3), Vec3(0.2, -0.3, 1), Vec3(1, 0, 0)), 4.0);
    const auto same = [](const Tessellation &a, const Tessellation &b) {
        if (a.faces.size() != b.faces.size() || a.failedFaces != b.failedFaces) return false;
        for (std::size_t f = 0; f < a.faces.size(); ++f) {
            const FaceMesh &x = a.faces[f], &y = b.faces[f];
            if (x.face != y.face || x.triangles != y.triangles || x.points.size() != y.points.size()) return false;
            for (std::size_t i = 0; i < x.points.size(); ++i)
                if (distance(x.points[i], y.points[i]) != 0.0 || distance(x.normals[i], y.normals[i]) != 0.0) return false;
        }
        return true;
    };
    for (const Body *input : {&body, &sphere}) {
        TessellationOptions options;
        options.deflection = 0.002;
        options.angle = 0.1;
        options.threads = 1;
        const Tessellation serial = tessellate(*input, options);
        FK_CHECK(serial.failedFaces == 0);
        options.threads = 0;
        FK_CHECK(same(serial, tessellate(*input, options)));
        CpuEvaluator evaluator;
        options.accelerator = &evaluator;
        options.acceleratorMinimumBatch = 16;
        FK_CHECK(same(serial, tessellate(*input, options)));
        FK_CHECK(evaluator.points > 0);
        FK_CHECK(evaluator.smallest >= 16);
        evaluator.accept = false;
        FK_CHECK(same(serial, tessellate(*input, options)));
    }
}
