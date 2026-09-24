#include "fk_revolve.h"

#include <cmath>
#include <stdexcept>

#include "fk_curve_ops.h"
#include "fk_pcurve.h"
#include "fk_precision.h"
#include "fk_surface_algo.h"

namespace ForgeCad::Kernel {
namespace {

const Curve<2> &basisOf(const Curve<2> &curve) {
    const Curve<2> *c = &curve;
    while (c->type() == CurveType::Trimmed) c = static_cast<const TrimmedCurve<2> *>(c)->basis().get();
    return *c;
}

}

Body makeRevolution(const Frame3 &frame, const ProfileRegion &region) {
    const double tolerance = 1e-9 * std::max(1.0, norm(frame.origin()));
    const Vec3 O = frame.origin(), X = frame.xDir(), Y = frame.yDir(), Z = frame.zDir();
    auto at = [&](const Vec2 &p) { return O + p.x() * X + p.y() * Z; };
    auto direction = [&](const Vec2 &d) { return d.x() * X + d.y() * Z; };

    std::vector<ProfileLoop> loops{signedArea(region.outer) > 0.0 ? region.outer : reversed(region.outer)};
    for (const ProfileLoop &hole : region.holes) loops.push_back(signedArea(hole) < 0.0 ? hole : reversed(hole));

    std::vector<Vec3> vertices;
    std::vector<Body::BuildEdge> edges;
    std::vector<Body::BuildFace> faces;
    for (const ProfileLoop &loop : loops) {
        const std::size_t n = loop.segments.size();
        // Edge circolare di ogni vertice fuori dall'asse (-1: sull'asse). Un
        // loop di una sola curva chiusa (cerchio: toro intero) non ha vertici.
        std::vector<int> circle(n, -1);
        for (std::size_t k = 0; k < n && !(n == 1 && distance(loop.segments[0].start(), loop.segments[0].end()) <= kLinearResolution); ++k) {
            const Vec2 p = loop.segments[k].start();
            if (p.x() < -tolerance) throw std::invalid_argument("makeRevolution: il profilo attraversa l'asse");
            if (p.x() <= tolerance) continue;
            Body::BuildEdge edge;
            edge.start = edge.end = int(vertices.size());
            vertices.push_back(at(p));
            edge.curve = std::make_shared<Circle<3>>(O + p.y() * Z, X, Y, p.x());
            edge.range = {0.0, kTwoPi};
            // Vertici che si toccano solo entro la tolleranza dello schizzo.
            const double gap = distance(loop.segments[(k + n - 1) % n].end(), p);
            if (gap > kLinearResolution) edge.tolerance = 1.01 * gap;
            circle[k] = int(edges.size());
            edges.push_back(edge);
        }
        for (std::size_t k = 0; k < n; ++k) {
            const ProfileSegment &segment = loop.segments[k];
            const Vec2 a = segment.start(), b = segment.end();
            const Vec2 middle = segment.curve->point(0.5 * (segment.range.lo + segment.range.hi));
            if (a.x() <= tolerance && b.x() <= tolerance && middle.x() <= tolerance) continue;  // sull'asse
            const Curve<2> &basis = basisOf(*segment.curve);
            SurfacePtr surface;
            if (basis.type() == CurveType::Line) {
                const Vec2 d = b - a;
                if (std::fabs(d.x()) <= 1e-12 * norm(d)) {
                    surface = std::make_shared<CylindricalSurface>(Frame3(O, Z, X), a.x());
                } else if (std::fabs(d.y()) <= 1e-12 * norm(d)) {
                    surface = std::make_shared<Plane>(Frame3(O + a.y() * Z, Z, X));
                } else {
                    // Generatrice orientata verso +Z: il semiangolo ha il coseno positivo.
                    const Vec2 g = d.y() > 0.0 ? d : -d;
                    surface = std::make_shared<ConicalSurface>(Frame3(O + a.y() * Z, Z, X), std::atan2(g.x(), g.y()), a.x());
                }
            } else if (basis.type() == CurveType::Circle) {
                const auto &c = static_cast<const Circle<2> &>(basis);
                if (std::fabs(c.center().x()) <= tolerance) surface = std::make_shared<SphericalSurface>(Frame3(O + c.center().y() * Z, Z, X), c.radius());
                else surface = std::make_shared<ToroidalSurface>(Frame3(O + c.center().y() * Z, Z, X), c.center().x(), c.radius());
            } else {
                surface = std::make_shared<RevolutionSurface>(embedCurve(segment.curve, Frame3(O, -Y, X)), O, Z);
            }
            // Normale uscente: a destra del verso di percorrenza (materiale a sinistra).
            Vec2 d[2];
            segment.curve->evaluate(0.5 * (segment.range.lo + segment.range.hi), 1, d);
            const Vec3 outward = direction(Vec2(d[1].y(), -d[1].x()));
            const SurfaceProjection onSurface = projectPoint(*surface, at(middle));
            Body::BuildFace face;
            face.surface = surface;
            face.sense = dot(surface->normal(onSurface.u, onSurface.v), outward) > 0.0;
            // Il cerchio all'inizio del tratto si percorre nel verso del suo
            // parametro, quello alla fine al contrario (la faccia a sinistra).
            if (circle[k] >= 0) face.loops.push_back({Body::BuildFin{circle[k], true, nullptr, 0.0}});
            if (circle[(k + 1) % n] >= 0) face.loops.push_back({Body::BuildFin{circle[(k + 1) % n], false, nullptr, 0.0}});
            faces.push_back(std::move(face));
        }
    }
    if (faces.empty()) throw std::invalid_argument("makeRevolution: profilo tutto sull'asse");
    Body body = Body::build(vertices, edges, faces);
    computePCurves(body);
    return body;
}

}
