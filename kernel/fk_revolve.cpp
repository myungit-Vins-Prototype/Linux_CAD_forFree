#include "fk_revolve.h"

#include <cmath>
#include <stdexcept>

#include "fk_boolean.h"
#include "fk_classify.h"
#include "fk_curve_ops.h"
#include "fk_extrude.h"
#include "fk_history.h"
#include "fk_intersect.h"
#include "fk_primitives.h"
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

ProfileRegion regionOf(const std::vector<ProfileSegment> &segments) {
    const Profile profile = buildProfile(segments, kLinearResolution);
    if (profile.regions.size() != 1) throw std::invalid_argument("profilo di rivoluzione non valido");
    return profile.regions.front();
}

ProfileSegment line2(const Vec2 &a, const Vec2 &b) { return {std::make_shared<Line<2>>(a, b - a), {0.0, distance(a, b)}}; }

}

namespace {

// Facce di rivoluzione (giro completo) dei tratti di `loops`: chiusi (`closed`,
// i loop di un solido) o catene aperte (lamina: i cerchi degli estremi restano
// bordi liberi). Normale della faccia a destra del verso di percorrenza.
void revolveLoops(const Frame3 &frame, const std::vector<ProfileLoop> &loops, bool closed, std::vector<Vec3> &vertices,
                  std::vector<Body::BuildEdge> &edges, std::vector<Body::BuildFace> &faces,
                  RevolutionProvenance *provenance, const std::vector<bool> &reversedLoops = {}, bool splitClosedMeridians = false) {
    const double tolerance = 1e-9 * std::max(1.0, norm(frame.origin()));
    const Vec3 O = frame.origin(), X = frame.xDir(), Y = frame.yDir(), Z = frame.zDir();
    auto at = [&](const Vec2 &p) { return O + p.x() * X + p.y() * Z; };
    auto direction = [&](const Vec2 &d) { return d.x() * X + d.y() * Z; };
    for (std::size_t loopIndex = 0; loopIndex < loops.size(); ++loopIndex) {
        ProfileLoop loop = loops[loopIndex];
        // Una B-spline chiusa ma non periodica genera al giro completo una
        // faccia senza bordi. Il taglio angolare richiede invece una cucitura
        // esplicita nel meridiano: due intervalli esatti della stessa curva,
        // con la medesima sorgente, senza ricampionare o alterare il profilo.
        const bool split = splitClosedMeridians && closed && loop.segments.size() == 1
            && !basisOf(*loop.segments.front().curve).isPeriodic()
            && distance(loop.segments.front().start(), loop.segments.front().end()) <= kLinearResolution;
        if (split) {
            const ProfileSegment original = loop.segments.front();
            const double middle = 0.5 * (original.range.lo + original.range.hi);
            loop.segments.clear();
            for (const Interval range : {Interval{original.range.lo, middle}, Interval{middle, original.range.hi}})
                loop.segments.push_back({std::make_shared<TrimmedCurve<2>>(original.curve, range.lo, range.hi), range, original.source});
        }
        const std::size_t n = loop.segments.size();
        if (n == 0) continue;
        // Punti della catena: n nei loop chiusi (il primo e' anche l'ultimo),
        // n + 1 nelle catene aperte.
        const std::size_t points = closed ? n : n + 1;
        const auto pointAt = [&](std::size_t k) { return k < n ? loop.segments[k].start() : loop.segments[n - 1].end(); };
        // Edge circolare di ogni vertice fuori dall'asse (-1: sull'asse). Un
        // loop di una sola curva chiusa (cerchio: toro intero) non ha vertici.
        std::vector<int> circle(points, -1);
        const bool singleClosedCurve = closed && n == 1 && distance(loop.segments[0].start(), loop.segments[0].end()) <= kLinearResolution;
        for (std::size_t k = 0; k < points && !singleClosedCurve; ++k) {
            const Vec2 p = pointAt(k);
            if (p.x() < -tolerance) throw std::invalid_argument("makeRevolution: il profilo attraversa l'asse");
            if (p.x() <= tolerance) continue;
            Body::BuildEdge edge;
            edge.start = edge.end = int(vertices.size());
            vertices.push_back(at(p));
            edge.curve = std::make_shared<Circle<3>>(O + p.y() * Z, X, Y, p.x());
            edge.range = {0.0, kTwoPi};
            // Vertici che si toccano solo entro la tolleranza dello schizzo.
            if (closed || (k > 0 && k < n)) {
                const double gap = distance(loop.segments[(k + n - 1) % n].end(), p);
                if (gap > kLinearResolution) edge.tolerance = 1.01 * gap;
            }
            circle[k] = int(edges.size());
            edges.push_back(edge);
        }
        for (std::size_t k = 0; k < n; ++k) {
            const ProfileSegment &segment = loop.segments[k];
            const Vec2 a = segment.start(), b = segment.end();
            const Vec2 middle = segment.curve->point(0.5 * (segment.range.lo + segment.range.hi));
            if (middle.x() < -tolerance) throw std::invalid_argument("makeRevolution: il profilo attraversa l'asse");
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
                else if (c.center().x() > 0.0)
                    surface = std::make_shared<ToroidalSurface>(Frame3(O + c.center().y() * Z, Z, X), c.center().x(), c.radius());
                else {
                    // Un arco puo' stare nel semipiano positivo anche con il
                    // centro oltre l'asse. Il raggio maggiore del toro sarebbe
                    // negativo: conserva il meridiano esatto e il suo intervallo,
                    // evitando gli altri rami del cerchio completo.
                    surface = std::make_shared<RevolutionSurface>(
                        embedCurve(std::make_shared<TrimmedCurve<2>>(segment.curve, segment.range.lo, segment.range.hi), Frame3(O, -Y, X)), O, Z);
                }
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
            const std::size_t next = closed ? (k + 1) % n : k + 1;
            if (circle[k] >= 0) face.loops.push_back({Body::BuildFin{circle[k], true, nullptr, 0.0}});
            if (circle[next] >= 0) face.loops.push_back({Body::BuildFin{circle[next], false, nullptr, 0.0}});
            if (provenance) {
                const int original = split ? 0 : !reversedLoops.empty() && reversedLoops[loopIndex] ? int(n - 1 - k) : int(k);
                provenance->faces.push_back({FaceId(int(faces.size())), RevolutionFaceRole::Side, int(loopIndex), original, segment.source});
            }
            faces.push_back(std::move(face));
        }
    }
}

// Il corpo del giro completo ridotto all'angolo `angle`: intersezione con il
// cuneo dei due semipiani per l'asse.
Body cutToAngle(Body full, const Frame3 &frame, double angle, RevolutionProvenance *provenance) {
    if (!(std::fabs(angle) < kTwoPi - kAngularResolution)) return full;
    if (std::fabs(angle) <= kAngularResolution) throw std::invalid_argument("makeRevolution: angolo nullo");
    // Estensione del corpo lungo l'asse e distanza massima dall'asse, dagli
    // angoli del suo box.
    Box box;
    for (VertexId v : full.vertices()) box.add(full.vertex(v).point);
    for (EdgeId e : full.edges()) box.add(curveBox(*full.edge(e).curve, full.edge(e).range));
    for (FaceId f : full.faces()) box.add(faceBox(full, f));
    const Vec3 O = frame.origin(), Z = frame.zDir();
    double zlo = 1e300, zhi = -1e300, radius = 0.0;
    for (int corner = 0; corner < 8; ++corner) {
        const Vec3 c((corner & 1) ? box.hi.x() : box.lo.x(), (corner & 2) ? box.hi.y() : box.lo.y(), (corner & 4) ? box.hi.z() : box.lo.z());
        const double z = dot(c - O, Z);
        zlo = std::min(zlo, z);
        zhi = std::max(zhi, z);
        radius = std::max(radius, norm(c - O - z * Z));
    }
    const double margin = 0.25 * (zhi - zlo + radius) + 1.0;
    // Cuneo: poligono con un vertice sull'asse e gli altri a distanza 2 R sui
    // raggi, al piu' 60 gradi l'uno dall'altro (le corde restano fuori dal
    // cerchio di raggio R), estruso oltre il corpo nei due versi.
    const double from = std::min(0.0, angle), to = std::max(0.0, angle);
    const int steps = std::max(1, int(std::ceil((to - from) / (kPi / 3.0))));
    const double reach = 2.0 * (radius + margin);
    std::vector<Vec2> wedge{Vec2(0.0, 0.0)};
    for (int i = 0; i <= steps; ++i) {
        const double a = from + (to - from) * i / steps;
        wedge.push_back(Vec2(reach * std::cos(a), reach * std::sin(a)));
    }
    const Frame3 base(O + (zlo - margin) * Z, Z, frame.xDir());
    ExtrusionProvenance cutterOrigins;
    const Body cutter = makePrism(base, wedge, {}, zhi - zlo + 2.0 * margin, provenance ? &cutterOrigins : nullptr);
    BodyHistory history;
    Body result = booleanOperation(full, cutter, BooleanOperation::Intersect, {}, provenance ? &history : nullptr);
    if (provenance) {
        RevolutionProvenance output;
        std::vector<std::vector<RevolutionFaceOrigin>> sources(std::size_t(history.operandSize(0, EntityKind::Face)));
        for (const auto &origin : provenance->faces) sources[std::size_t(origin.face.index)].push_back(origin);
        std::vector<int> capRoles(std::size_t(history.operandSize(1, EntityKind::Face)), -1);
        // Il primo e l'ultimo lato del poligono sono i due raggi del cuneo.
        // La loro identita' viene dall'estrusione, senza cercare piani coincidenti.
        for (const auto &origin : cutterOrigins.faces) {
            if (origin.role != ExtrusionFaceRole::Side || origin.loop != 0) continue;
            if (origin.segment != 0 && origin.segment != steps + 1) continue;
            const bool start = angle > 0 ? origin.segment == 0 : origin.segment == steps + 1;
            capRoles[std::size_t(origin.face.index)] = int(start ? RevolutionFaceRole::Start : RevolutionFaceRole::End);
        }
        for (FaceId f : result.faces()) for (const HistoryLink &link : history.origins(EntityKind::Face, f.index)) {
            if (!history.isDeclared(EntityKind::Face, f.index) || link.kind != EntityKind::Face) continue;
            if (link.operand == 0) {
                for (auto origin : sources[std::size_t(link.index)]) {
                    origin.face = f;
                    output.faces.push_back(origin);
                }
            } else if (link.operand == 1) {
                const int role = capRoles[std::size_t(link.index)];
                if (role >= 0) output.faces.push_back({f, RevolutionFaceRole(role)});
            }
        }
        *provenance = std::move(output);
    }
    return result;
}

}

static Body makeFullRevolution(const Frame3 &frame, const ProfileRegion &region, RevolutionProvenance *provenance,
                               bool splitClosedMeridians = false) {
    if (provenance) *provenance = {};
    RevolutionProvenance output;
    std::vector<ProfileLoop> loops{signedArea(region.outer) > 0.0 ? region.outer : reversed(region.outer)};
    std::vector<bool> reversedLoops{signedArea(region.outer) <= 0.0};
    for (const ProfileLoop &hole : region.holes) {
        loops.push_back(signedArea(hole) < 0.0 ? hole : reversed(hole));
        reversedLoops.push_back(signedArea(hole) >= 0.0);
    }
    std::vector<Vec3> vertices;
    std::vector<Body::BuildEdge> edges;
    std::vector<Body::BuildFace> faces;
    revolveLoops(frame, loops, true, vertices, edges, faces, provenance ? &output : nullptr, reversedLoops, splitClosedMeridians);
    if (faces.empty()) throw std::invalid_argument("makeRevolution: profilo tutto sull'asse");
    Body body = Body::build(vertices, edges, faces);
    computePCurves(body);
    if (provenance) *provenance = std::move(output);
    return body;
}

Body makeSheetRevolution(const Frame3 &frame, const std::vector<ProfileLoop> &chains, double angle, RevolutionProvenance *provenance) {
    if (provenance) *provenance = {};
    RevolutionProvenance output;
    std::vector<Vec3> vertices;
    std::vector<Body::BuildEdge> edges;
    std::vector<Body::BuildFace> faces;
    revolveLoops(frame, chains, false, vertices, edges, faces, provenance ? &output : nullptr);
    if (faces.empty()) throw std::invalid_argument("makeSheetRevolution: profilo tutto sull'asse");
    Body body = Body::buildSheet(vertices, edges, faces);
    computePCurves(body);
    body = cutToAngle(std::move(body), frame, angle, provenance ? &output : nullptr);
    if (provenance) *provenance = std::move(output);
    return body;
}

Body makeRevolution(const Frame3 &frame, const ProfileRegion &region, double angle, RevolutionProvenance *provenance) {
    if (provenance) *provenance = {};
    RevolutionProvenance output;
    Body body = makeFullRevolution(frame, region, provenance ? &output : nullptr,
                                   std::fabs(angle) < kTwoPi - kAngularResolution);
    body = cutToAngle(std::move(body), frame, angle, provenance ? &output : nullptr);
    if (provenance) *provenance = std::move(output);
    return body;
}

Body makeRevolution(const Frame3 &frame, const ProfileRegion &region) {
    return makeFullRevolution(frame, region, nullptr);
}

Body makeSphere(const Frame3 &frame, double radius, FaceAliases *labels) {
    if(labels)labels->clear();
    if (!(radius > kLinearResolution)) throw std::invalid_argument("makeSphere: raggio non valido");
    auto arc = std::make_shared<Circle<2>>(makeCircle(Vec2(0.0, 0.0), radius));
    Body result=makeRevolution(frame,regionOf({{arc,{-kHalfPi,kHalfPi}},line2(Vec2(0.0,radius),Vec2(0.0,-radius))}));
    if(labels)*labels={{"sphere"}};
    return result;
}

Body makeCone(const Frame3 &frame, double baseRadius, double topRadius, double height, FaceAliases *labels) {
    if(labels)labels->clear();
    if (!(height > kLinearResolution) || baseRadius < 0.0 || topRadius < 0.0 || std::max(baseRadius, topRadius) <= kLinearResolution)
        throw std::invalid_argument("makeCone: dimensioni non valide");
    std::vector<Vec2> points{Vec2(0.0, 0.0)};
    if (baseRadius > kLinearResolution) points.push_back(Vec2(baseRadius, 0.0));
    if (topRadius > kLinearResolution) points.push_back(Vec2(topRadius, height));
    points.push_back(Vec2(0.0, height));
    std::vector<ProfileSegment> segments;
    for (std::size_t i = 0; i < points.size(); ++i) segments.push_back(line2(points[i], points[(i + 1) % points.size()]));
    const auto region=regionOf(segments);RevolutionProvenance origins;
    Body result=makeRevolution(frame,region,kTwoPi,labels?&origins:nullptr);
    if(labels) {
        labels->resize(result.faces().size());
        for(const auto &o:origins.faces) {
            const auto &segment=region.outer.segments.at(std::size_t(o.segment));const auto a=segment.start(),b=segment.end();
            (*labels)[std::size_t(o.face.index)]={a.y()==b.y()?(a.y()==0?"-z":"+z"):(baseRadius==topRadius?"cylinder":"cone")};
        }
    }
    return result;
}

Body makeTorus(const Frame3 &frame, double majorRadius, double minorRadius, FaceAliases *labels) {
    if(labels)labels->clear();
    if (!(minorRadius > kLinearResolution) || !(majorRadius > minorRadius + kLinearResolution))
        throw std::invalid_argument("makeTorus: il raggio minore deve essere positivo e minore del maggiore");
    auto circle = std::make_shared<Circle<2>>(makeCircle(Vec2(majorRadius, 0.0), minorRadius));
    Body result=makeRevolution(frame,regionOf({{circle,{0.0,kTwoPi}}}));
    if(labels)*labels={{"torus"}};
    return result;
}

}
