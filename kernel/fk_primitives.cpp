#include "fk_primitives.h"

#include <algorithm>
#include <memory>
#include <stdexcept>

#include "fk_extrude.h"
#include "fk_precision.h"

namespace ForgeCad::Kernel {
namespace {

double signedArea(const std::vector<Vec2> &polygon) {
    double area = 0.0;
    for (std::size_t i = 0; i < polygon.size(); ++i) area += cross(polygon[i], polygon[(i + 1) % polygon.size()]);
    return 0.5 * area;
}

// Poligono in senso antiorario.
std::vector<Vec2> counterClockwise(std::vector<Vec2> polygon) {
    if (polygon.size() < 3) throw std::invalid_argument("makePrism: servono almeno 3 vertici");
    const double area = signedArea(polygon);
    if (std::fabs(area) <= kLinearResolution * kLinearResolution) throw std::invalid_argument("makePrism: poligono degenere");
    if (area < 0.0) std::reverse(polygon.begin(), polygon.end());
    return polygon;
}

}

void assignPolyhedralGeometry(Body &body) {
    for (EdgeId e : body.edges()) {
        Edge &edge = body.edge(e);
        if (edge.curve) continue;
        const Vec3 start = body.vertex(body.edgeStart(e)).point, end = body.vertex(body.edgeEnd(e)).point;
        const double length = distance(start, end);
        if (!(length > kLinearResolution)) throw std::logic_error("assignPolyhedralGeometry: edge di lunghezza nulla");
        edge.curve = std::make_shared<Line<3>>(start, end - start);
        edge.range = {0.0, length};
    }
    for (FaceId f : body.faces()) {
        Face &face = body.face(f);
        if (face.surface) continue;
        // Normale di Newell: somma dei p_i x p_{i+1} su tutti i loop. I fori,
        // percorsi al contrario, sottraggono la loro area senza cambiare verso.
        Vec3 newell;
        Vec3 origin, xDirection;
        bool first = true;
        for (LoopId l : face.loops) {
            const std::vector<FinId> fins = body.loopFins(l);
            for (FinId fin : fins) {
                const Vec3 a = body.vertex(body.finStart(fin)).point, b = body.vertex(body.finEnd(fin)).point;
                newell += cross(a, b);
                if (first) {
                    origin = a;
                    xDirection = b - a;
                    first = false;
                }
            }
        }
        if (first || !(norm(newell) > kLinearResolution * kLinearResolution))
            throw std::logic_error("assignPolyhedralGeometry: faccia degenere");
        face.surface = std::make_shared<Plane>(Frame3(origin, newell, xDirection));
        face.sense = true;
    }
}

namespace {

// Loop di segmenti di retta tra i vertici consecutivi del poligono.
ProfileLoop polygonLoop(const std::vector<Vec2> &polygon) {
    ProfileLoop loop;
    for (std::size_t i = 0; i < polygon.size(); ++i) {
        const Vec2 a = polygon[i], b = polygon[(i + 1) % polygon.size()];
        const double length = distance(a, b);
        if (!(length > kLinearResolution)) throw std::invalid_argument("makePrism: vertici coincidenti");
        loop.segments.push_back({std::make_shared<Line<2>>(a, b - a), {0.0, length}});
    }
    return loop;
}

}

Body makePrism(const Frame3 &frame, const std::vector<Vec2> &outerPolygon, const std::vector<std::vector<Vec2>> &holePolygons,
               double height) {
    ProfileRegion region;
    region.outer = polygonLoop(counterClockwise(outerPolygon));
    for (const std::vector<Vec2> &hole : holePolygons) region.holes.push_back(reversed(polygonLoop(counterClockwise(hole))));
    return makeExtrusion(frame, region, height);
}

Body makeBox(const Frame3 &frame, double dx, double dy, double dz) {
    if (!(dx > kLinearResolution && dy > kLinearResolution)) throw std::invalid_argument("makeBox: dimensioni non valide");
    return makePrism(frame, {Vec2(0, 0), Vec2(dx, 0), Vec2(dx, dy), Vec2(0, dy)}, {}, dz);
}

// Un cerchio come unico tratto: il fianco resta senza cucitura.
Body makeCylinder(const Frame3 &frame, double radius, double height) {
    if (!(radius > kLinearResolution)) throw std::invalid_argument("makeCylinder: raggio non valido");
    ProfileRegion region;
    region.outer.segments.push_back({std::make_shared<Circle<2>>(makeCircle(Vec2(), radius)), {0.0, kTwoPi}});
    return makeExtrusion(frame, region, height);
}

}
