#include "cad_extrude.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <map>
#include <memory>

#include "cad_datum.h"
#include "cad_element_names.h"
#include "cad_feature_dependencies.h"
#include "cad_forge.h"
#include "cad_kernel.h"
#include "fk_boolean.h"
#include "fk_bspline.h"
#include "fk_classify.h"
#include "fk_intersect.h"
#include "fk_face_normal_bounds.h"
#include "fk_mass.h"
#include "fk_primitives.h"
#include "fk_surface.h"

namespace ForgeCad {
namespace {

using namespace Kernel;

void setError(QString *error, const QString &message) {
    if (error) *error = message;
}

Box bodyBox(const Body &body) {
    Box box;
    for (FaceId f : body.faces()) box.add(faceBox(body, f));
    for (VertexId v : body.vertices()) box.add(body.vertex(v).point);
    return box;
}

std::vector<Vec3> corners(const Box &box) {
    std::vector<Vec3> result;
    for (int k = 0; k < 8; ++k)
        result.emplace_back(k & 1 ? box.hi.x() : box.lo.x(), k & 2 ? box.hi.y() : box.lo.y(), k & 4 ? box.hi.z() : box.lo.z());
    return result;
}

// La shell ha una faccia piana nel piano dello schizzo (la base dell'estrusione).
bool touchesPlane(const Body &body, ShellId shell, const Vec3 &origin, const Vec3 &normal, double tolerance) {
    for (FaceId f : body.shell(shell).faces) {
        const auto *plane = dynamic_cast<const Plane *>(body.face(f).surface.get());
        if (!plane) continue;
        const Vec3 n = plane->frame().zDir();
        if (std::fabs(std::fabs(dot(n, normal)) - 1.0) > 1e-9) continue;
        if (std::fabs(dot(plane->frame().origin() - origin, normal)) <= tolerance) return true;
    }
    return false;
}

// Distanza con segno lungo la direzione dell'estrusione fino al piano (n, q)
// da tutti gli angoli del profilo (il box di un'estrusione di altezza unitaria).
bool planeReach(const Box &profile, const Vec3 &origin, const Vec3 &e, const Vec3 &q, const Vec3 &n, double &lo, double &hi) {
    const double along = dot(e, n);
    if (std::fabs(along) < 1e-9) return false;
    lo = std::numeric_limits<double>::max();
    hi = -lo;
    for (const Vec3 &corner : corners(profile)) {
        const Vec3 base = corner - dot(corner - origin, e) * e;  // sul piano dello schizzo
        const double t = dot(q - base, n) / along;
        lo = std::min(lo, t);
        hi = std::max(hi, t);
    }
    return true;
}

}

namespace {

// Le shell scelte di `body` aggiunte alle liste di Body::build.
void appendShells(const Body &source, const std::function<bool(const Body &, ShellId)> &keep, std::vector<Vec3> &vertices,
                  std::vector<Body::BuildEdge> &edges, std::vector<Body::BuildFace> &faces,
                  std::vector<HistoryLink> *origins = nullptr, int operand = 0) {
    const Body *body = &source;
    std::map<int, int> vertexMap, edgeMap;
    const auto vertexIndex = [&](VertexId v) {
        const auto found = vertexMap.find(v.index);
        if (found != vertexMap.end()) return found->second;
        vertices.push_back(body->vertex(v).point);
        return vertexMap[v.index] = int(vertices.size()) - 1;
    };
    const auto edgeIndex = [&](EdgeId e) {
        const auto found = edgeMap.find(e.index);
        if (found != edgeMap.end()) return found->second;
        const Edge &edge = body->edge(e);
        Body::BuildEdge built;
        built.start = vertexIndex(body->edgeStart(e));
        built.end = vertexIndex(body->edgeEnd(e));
        built.curve = edge.curve;
        built.range = edge.range;
        built.tolerance = edge.tolerance;
        edges.push_back(built);
        return edgeMap[e.index] = int(edges.size()) - 1;
    };
    for (ShellId shell : body->shells()) {
        if (!keep(source, shell)) continue;
        for (FaceId f : body->shell(shell).faces) {
            const Face &face = body->face(f);
            Body::BuildFace built;
            built.surface = face.surface;
            built.sense = face.sense;
            for (LoopId l : face.loops) {
                std::vector<Body::BuildFin> loop;
                if (!body->loop(l).first.valid()) continue;
                for (FinId fin : body->loopFins(l)) {
                    const Fin &data = body->fin(fin);
                    Body::BuildFin b;
                    b.edge = edgeIndex(data.edge);
                    b.sense = data.sense;
                    b.pcurve = data.pcurve;
                    b.pcurveTolerance = data.pcurveTolerance;
                    loop.push_back(b);
                }
                built.loops.push_back(std::move(loop));
            }
            faces.push_back(std::move(built));
            if (origins) origins->push_back({operand, EntityKind::Face, f.index, HistoryRelation::Same});
        }
    }
}

// Due solidi in un body solo senza booleana (pezzi separati, anche se si
// toccano lungo uno spigolo): quando l'unione non si puo' fare.
Body combined(const Body &a, const Body &b, BodyHistory *history) {
    std::vector<Vec3> vertices;
    std::vector<Body::BuildEdge> edges;
    std::vector<Body::BuildFace> faces;
    const auto all = [](const Body &, ShellId) { return true; };
    std::vector<HistoryLink> origins;
    appendShells(a, all, vertices, edges, faces, history ? &origins : nullptr, 0);
    appendShells(b, all, vertices, edges, faces, history ? &origins : nullptr, 1);
    Body result = Body::build(vertices, edges, faces);
    if (history) {
        *history = makeHistory({&a, &b}, result);
        const auto built = result.faces();
        if (built.size() != origins.size()) throw std::logic_error("estrusione: mappa dei pezzi incoerente");
        for (std::size_t i = 0; i < built.size(); ++i) history->declareOrigins(EntityKind::Face, built[i].index, {origins[i]});
    }
    return result;
}


// L'utensile del limite produce il coperchio finale; quando e' un corpo
// effettivo il legame Generated mantiene anche la faccia di riferimento.
ForgeBody extrusionEndTool(Body body, const ForgeBody &owner = {}, int sourceFace = -1) {
    BodyHistory history = makeHistory(owner ? std::vector<const Body *>{owner.get()} : std::vector<const Body *>{}, body);
    FaceAliases labels(std::size_t(history.resultSize(EntityKind::Face)));
    for (FaceId face : body.faces()) {
        std::vector<HistoryLink> origins;
        if (owner) origins.push_back({0, EntityKind::Face, sourceFace >= 0 ? sourceFace : face.index, HistoryRelation::Generated});
        history.declareOrigins(EntityKind::Face, face.index, std::move(origins));
        labels[std::size_t(face.index)] = {"cap.end"};
    }
    const ForgeBody result = std::make_shared<const Body>(std::move(body));
    registerDeclaredHistory(result, owner ? std::vector<ForgeBody>{owner} : std::vector<ForgeBody>{}, std::move(history), std::move(labels));
    return result;
}

}

ForgeBody forgeKeepShells(const ForgeBody &body, const std::function<bool(const Body &, ShellId)> &keep) {
    if (!body) return nullptr;
    std::vector<Vec3> vertices;
    std::vector<Body::BuildEdge> edges;
    std::vector<Body::BuildFace> faces;
    std::vector<HistoryLink> origins;
    appendShells(*body, keep, vertices, edges, faces, &origins);
    if (faces.empty()) return nullptr;
    const ForgeBody result = std::make_shared<const Body>(Body::build(vertices, edges, faces));
    BodyHistory history = makeHistory({body.get()}, *result);
    const auto built = result->faces();
    if (built.size() != origins.size()) throw std::logic_error("estrusione: mappa delle shell incoerente");
    for (std::size_t i = 0; i < built.size(); ++i) history.declareOrigins(EntityKind::Face, built[i].index, {origins[i]});
    registerDeclaredHistory(result, {body}, std::move(history));
    return result;
}

bool forgeSharesMaterial(const ForgeBody &tool, const ForgeBody &body, bool subtract) {
    if (!tool || !body || tool->isSheet() || body->isSheet()) return false;
    const double scale = std::max(1.0, bodyBox(*tool).diagonal());
    if (!bodyBox(*tool).padded(1e-6 * scale).overlaps(bodyBox(*body))) return false;
    try {
        if (subtract) return !booleanOperation(*tool, *body, Kernel::BooleanOperation::Intersect).faces().empty();
        const Body united = booleanOperation(*tool, *body, Kernel::BooleanOperation::Unite);
        return united.shells().size() < tool->shells().size() + body->shells().size();
    } catch (const std::exception &) {
        return false;
    }
}

namespace {

struct DisjointLimit { int index; ForgeBody geometry; };
using DisjointLimits = QVector<DisjointLimit>;
ForgeBody mergeFeatureResult(ExtrusionObject &body, const ForgeBody &result, int index,
                             const QVector<ExtrusionObject> &bodies, QString *error,
                             const DisjointLimits &disjoint);

Interval curveProjection(const Curve<3> &curve, const Interval &range, const Vec3 &origin, const Vec3 &direction) {
    if (direction.x() == 0 && direction.y() == 0 && direction.z() == 0) return {0,0};
    Interval result{std::numeric_limits<double>::max(), -std::numeric_limits<double>::max()};
    const auto add = [&](double value) { result.lo = std::min(result.lo,value); result.hi = std::max(result.hi,value); };
    const auto point = [&](const Vec3 &p) { add(dot(p-origin,direction)); };
    const auto conic = [&](const Vec3 &center, const Vec3 &x, const Vec3 &y) {
        const double c = dot(center-origin,direction), a = dot(x,direction), b = dot(y,direction);
        const auto value = [&](double t) { return c + a*std::cos(t) + b*std::sin(t); };
        const double amplitude = std::hypot(a,b);
        if (!range.isFinite() || range.length() >= kTwoPi) { add(c-amplitude); add(c+amplitude); return; }
        add(value(range.lo)); add(value(range.hi));
        const double phase = std::atan2(b,a);
        const double first = std::ceil((range.lo-phase)/kPi), last = std::floor((range.hi-phase)/kPi);
        for (int step = 0; step < 3 && first+step <= last; ++step) add(value(phase+(first+step)*kPi));
    };
    switch (curve.type()) {
    case CurveType::Line: point(curve.point(range.lo)); point(curve.point(range.hi)); break;
    case CurveType::Circle: {
        const auto &c = static_cast<const Circle<3> &>(curve);
        conic(c.center(), c.radius()*c.xAxis(), c.radius()*c.yAxis()); break;
    }
    case CurveType::Ellipse: {
        const auto &c = static_cast<const Ellipse<3> &>(curve);
        conic(c.center(), c.xRadius()*c.xAxis(), c.yRadius()*c.yAxis()); break;
    }
    case CurveType::BSpline:
        for (const auto &pole : static_cast<const BSplineCurve<3> &>(curve).poles()) point(pole);
        break;
    case CurveType::Trimmed:
        return curveProjection(*static_cast<const TrimmedCurve<3> &>(curve).basis(),range,origin,direction);
    case CurveType::Transformed: {
        const auto &transformed = static_cast<const TransformedCurve &>(curve);
        const auto &transform = transformed.transform();
        Vec3 local;
        for (int j=0;j<3;++j) for (int i=0;i<3;++i) local[j] += transform.matrix(i,j)*direction[i];
        const auto projected = curveProjection(*transformed.basis(),range,Vec3(),local);
        const double offset = dot(transform.applyToPoint(Vec3())-origin,direction);
        return {projected.lo+offset,projected.hi+offset};
    }
    default: return {-std::numeric_limits<double>::infinity(),std::numeric_limits<double>::infinity()};
    }
    return result;
}

// Bounds conservativi di una funzione lineare su una faccia analitica.
// Sui cilindri e sui coni gli estremi raggiungono il bordo lungo una
// generatrice. Sulla sfera si aggiungono i due punti stazionari interni.
Interval faceProjection(const Body &body, FaceId face, const Vec3 &origin, const Vec3 &direction) {
    Interval range{std::numeric_limits<double>::max(), -std::numeric_limits<double>::max()};
    const auto add = [&](const Vec3 &p) {
        const double value = dot(p - origin, direction);
        range.lo = std::min(range.lo, value); range.hi = std::max(range.hi, value);
    };
    for (LoopId loop : body.face(face).loops) {
        for (FinId fin : body.loopFins(loop)) {
            const auto &edge = body.edge(body.fin(fin).edge);
            const auto projected = curveProjection(*edge.curve,edge.range,origin,direction);
            range.lo = std::min(range.lo,projected.lo); range.hi = std::max(range.hi,projected.hi);
        }
        if (body.loop(loop).isolatedVertex.valid()) add(body.vertex(body.loop(loop).isolatedVertex).point);
    }
    const auto *sphere = dynamic_cast<const SphericalSurface *>(body.face(face).surface.get());
    if (sphere && norm(direction) > angularResolution()) {
        for (double sign : {-1.0, 1.0}) {
            const Vec3 point = sphere->frame().origin() + sign * sphere->radius() * normalized(direction);
            if (classifyPointOnFace(body, face, point, linearResolution()) != PointLocation::Outside) add(point);
        }
    }
    if (range.lo > range.hi) for (const auto &point : corners(faceBox(body, face))) add(point);
    return range;
}

// Tutte le facce diverse dalla base devono uscire nel verso scelto oppure
// essergli tangenti. Altrimenti il pezzo ha oltrepassato un primo ostacolo
// passando di lato: la sola connessione al piano iniziale non basta.
bool firstSurfaceEnvelope(const Body &body, const Vec3 &origin, const Vec3 &direction) {
    for (FaceId id : body.faces()) {
        const auto &face = body.face(id);
        const double sense = face.sense ? 1.0 : -1.0;
        if (const auto *plane = dynamic_cast<const Plane *>(face.surface.get())) {
            const double normal = sense * dot(plane->frame().zDir(), direction);
            if (normal >= -angularResolution()) continue;
            if (std::fabs(normal + 1.0) <= angularResolution()
                && std::fabs(dot(plane->frame().origin() - origin, direction)) <= linearResolution()) continue;
            return false;
        }
        if (const auto *cylinder = dynamic_cast<const CylindricalSurface *>(face.surface.get())) {
            const auto &frame = cylinder->frame();
            const Vec3 radial = direction - dot(direction, frame.zDir()) * frame.zDir();
            const Interval projected = faceProjection(body, id, frame.origin(), sense * radial);
            if (projected.lo / cylinder->radius() >= -angularResolution()) continue;
            return false;
        }
        if (const auto *sphere = dynamic_cast<const SphericalSurface *>(face.surface.get())) {
            if (faceProjection(body, id, sphere->frame().origin(), sense * direction).lo / sphere->radius() >= -angularResolution()) continue;
            return false;
        }
        if (const auto *cone = dynamic_cast<const ConicalSurface *>(face.surface.get())) {
            const auto &frame = cone->frame();
            const double sine = std::sin(cone->semiAngle()), cosine = std::cos(cone->semiAngle());
            const Interval axial = faceProjection(body, id, frame.origin(), frame.zDir());
            const double r0 = cone->referenceRadius() + axial.lo * sine / cosine;
            const double r1 = cone->referenceRadius() + axial.hi * sine / cosine;
            if (std::min(r0, r1) < -linearResolution() && std::max(r0, r1) > linearResolution()) return false;
            const double branch = std::max(r0, r1) > linearResolution() ? 1.0 : -1.0;
            const Vec3 radial = direction - dot(direction, frame.zDir()) * frame.zDir();
            const double minimumRadius = std::min(std::fabs(r0), std::fabs(r1));
            const double maximumRadius = std::max(std::fabs(r0), std::fabs(r1));
            double minimum = -norm(radial), maximum = norm(radial);
            if (minimumRadius > linearResolution()) {
                const Interval numerator = faceProjection(body, id, frame.origin(), radial);
                const double values[] = {numerator.lo / minimumRadius, numerator.lo / maximumRadius,
                                         numerator.hi / minimumRadius, numerator.hi / maximumRadius};
                minimum = std::max(minimum, *std::min_element(std::begin(values), std::end(values)));
                maximum = std::min(maximum, *std::max_element(std::begin(values), std::end(values)));
            }
            const double axialNormal = branch * sine * dot(frame.zDir(), direction);
            const double lower = sense > 0 ? cosine * minimum - axialNormal : axialNormal - cosine * maximum;
            if (lower >= -angularResolution()) continue;
            return false;
        }
        if (face.surface->type() == SurfaceType::Extrusion || face.surface->type() == SurfaceType::BSpline)
            if (faceHasNonnegativeNormalProjection(body, id, direction)) continue;
        return false;
    }
    return true;
}

ForgeBody extrusionUntilNext(ExtrusionObject &body, int index, const SketchObject &sketch,
                             const QVector<ExtrusionObject> &bodies, QString *error, DisjointLimits &disjoint) {
    if (body.extrudeSides != 0 || body.extrudeSurface) {
        setError(error, QStringLiteral("Fino al prossimo corpo richiede un profilo chiuso solido e un solo verso."));
        return nullptr;
    }
    if (!std::isfinite(body.distance)) {
        setError(error, QStringLiteral("Il verso dell'estrusione non e' valido.")); return nullptr;
    }
    const double side = body.distance < 0 ? -1.0 : 1.0;
    const Profile profile = buildProfile(forgeSketchSegments(sketch), sketchConnectionTolerance());
    if (profile.regions.empty() || profile.openChains > 0) {
        setError(error, QStringLiteral("Fino al prossimo corpo richiede solo contorni chiusi del profilo.")); return nullptr;
    }
    const Vec3 origin = sketchAxes(sketch).origin();
    const Vec3 direction = side * normalized(extrusionVector(sketch, 1.0));
    const ForgeBody unit = forgeExtrusion(sketch, side, error);
    if (!unit) return nullptr;
    if (unit->isSheet()) {
        setError(error, QStringLiteral("Fino al prossimo corpo richiede un profilo chiuso solido.")); return nullptr;
    }
    if (body.booleanTools.isEmpty()) body.booleanTools = nextExtrusionCandidates(index, bodies);
    struct Candidate { int index; double start; };
    QVector<Candidate> candidates;
    double reach = 0;
    for (int target : body.booleanTools) {
        if (target < 0 || target >= index || target >= bodies.size() || !bodies.at(target).forgeBody || !bodies.at(target).error.isEmpty()) {
            setError(error, QStringLiteral("Un corpo limite precedente non ha geometria valida.")); return nullptr;
        }
        const auto &shape = *bodies.at(target).forgeBody;
        if (shape.isSheet()) continue;
        double lo = std::numeric_limits<double>::max(), hi = -lo;
        for (const auto &point : corners(bodyBox(shape))) {
            const double projected = dot(point - origin, direction);
            lo = std::min(lo, projected); hi = std::max(hi, projected);
        }
        if (hi <= linearResolution()) {
            // Solo il semispazio davvero dietro il profilo prova che gli
            // interni non si intersecano. La fascia (0,resolution] non basta.
            if (hi <= 0.0) disjoint.append({target, bodies.at(target).forgeBody});
            continue;
        }
        candidates.append({target, lo}); reach = std::max(reach, hi);
    }
    if (candidates.isEmpty()) {
        setError(error, QStringLiteral("Nessun corpo solido precedente nel verso dell'estrusione.")); return nullptr;
    }
    std::stable_sort(candidates.begin(), candidates.end(), [](const Candidate &a, const Candidate &b) { return a.start < b.start; });
    // La bbox serve solo a dimensionare il prisma di ricerca. Tutti i limiti
    // effettivi e la copertura sono decisi dalle booleane sulle superfici esatte.
    const double length = reach + std::max(1.0, bodyBox(*unit).diagonal() + reach) * 0.01;
    ForgeBody result = forgeExtrusion(sketch, side * length, error);
    if (!result) return nullptr;
    const auto base = [&](const Body &shape, ShellId shell) { return touchesPlane(shape, shell, origin, direction, linearResolution()); };
    bool encountered = false;
    for (const auto &candidate : candidates) {
        const ForgeBody tool = extrusionEndTool(*bodies.at(candidate.index).forgeBody, bodies.at(candidate.index).forgeBody);
        const ForgeBody overlap = forgeBooleanWithHistory(result, tool, Kernel::BooleanOperation::Intersect);
        if (overlap->faces().empty()) {
            disjoint.append({candidate.index, bodies.at(candidate.index).forgeBody});
            continue;
        }
        encountered = true;
        for (ShellId shell : overlap->shells()) if (base(*overlap, shell)) {
            setError(error, QStringLiteral("Un corpo limite attraversa il piano iniziale del profilo.")); return nullptr;
        }
        const ForgeBody cut = forgeBooleanWithHistory(result, tool, Kernel::BooleanOperation::Subtract);
        // La selezione delle sole shell iniziali non deve riempire una
        // cavita': una shell interna ha volume orientato negativo. Rifiutarla
        // prima del filtro conserva il controllo degli ostacoli interni,
        // senza una seconda booleana su facce di taglio coincidenti.
        for (ShellId shell : cut->shells()) {
            if (base(*cut, shell)) continue;
            const ForgeBody component = forgeKeepShells(cut, [&](const Body &, ShellId candidate) { return candidate == shell; });
            if (!component || !(massProperties(*component).volume > 0.0)) {
                setError(error, QStringLiteral("Un ostacolo interno al profilo impedisce di certificare il limite alle prime superfici incontrate."));
                return nullptr;
            }
        }
        result = forgeKeepShells(cut, base);
        // Il filtro conserva un sottoinsieme della differenza solo dopo
        // avere escluso shell di cavita': prova valida sugli stessi B-rep.
        disjoint.append({candidate.index, bodies.at(candidate.index).forgeBody});
        if (!result) {
            setError(error, QStringLiteral("L'estrusione fino al prossimo corpo non ha una parte che parte dal profilo.")); return nullptr;
        }
    }
    if (!encountered) {
        setError(error, QStringLiteral("Nessun corpo incontra il profilo nel verso dell'estrusione.")); return nullptr;
    }
    const Vec3 far = origin + length * direction;
    for (ShellId shell : result->shells()) if (touchesPlane(*result, shell, far, direction, linearResolution())) {
        setError(error, QStringLiteral("Il profilo non e' interamente coperto dai corpi incontrati nel verso scelto.")); return nullptr;
    }
    if (!firstSurfaceEnvelope(*result, origin, direction)) {
        setError(error, QStringLiteral("Le superfici incontrate hanno sottosquadri o una forma non certificabile fino al primo incontro.")); return nullptr;
    }
    return result;
}

}

ForgeBody forgeExtrusionFeature(ExtrusionObject &body, int index, const QVector<SketchObject> &sketches, const QVector<ExtrusionObject> &bodies,
                                QString *error) {
    if (body.sketchIndex < 0 || body.sketchIndex >= sketches.size()) {
        setError(error, QStringLiteral("Lo schizzo del corpo non esiste piu'."));
        return nullptr;
    }
    const SketchObject &sketch = sketches.at(body.sketchIndex);
    // Con "Solo superficie" anche i contorni chiusi danno una lamina.
    const auto extrude = [&](double d, QString *why, double start = 0.0) {
        return forgeExtrusion(sketch, d, why, start, body.extrudeSurface);
    };
    ForgeBody result;
    DisjointLimits disjoint;
    try {
        if (body.extent == 4) {
            result = extrusionUntilNext(body, index, sketch, bodies, error, disjoint);
        } else if (body.extent == 0) {
            const double d = body.distance;
            if (body.extrudeSides == 1) {
                // Simmetrica: meta' per parte del piano dello schizzo.
                result = extrude(d, error, -0.5 * d);
            } else if (body.extrudeSides == 2) {
                if (std::fabs(d) <= 1e-7) {
                    setError(error, QStringLiteral("La distanza di estrusione e' nulla."));
                    return nullptr;
                }
                if (!(body.distance2 > 1e-7)) {
                    setError(error, QStringLiteral("La distanza nel secondo verso deve essere positiva."));
                    return nullptr;
                }
                // Un solo prisma dal piano spostato di distance2 all'indietro.
                const double s = d > 0.0 ? 1.0 : -1.0;
                result = extrude(d + s * body.distance2, error, -s * body.distance2);
            } else {
                result = extrude(d, error);
            }
        } else {
            if (body.extrudeSides == 1) {
                setError(error, QStringLiteral("L'estrusione simmetrica vale solo con la fine a distanza."));
                return nullptr;
            }
            ResolvedRef target;
            QString why;
            if (!resolveGeometryRef(body.extentRef, index, sketches, bodies, target, &why)) {
                setError(error, QStringLiteral("Riferimento della fine non valido: %1.").arg(why.isEmpty() ? QStringLiteral("da scegliere") : why));
                return nullptr;
            }
            const Frame3 frame = sketchAxes(sketch);
            const Vec3 origin = frame.origin();
            const Vec3 e = normalized(extrusionVector(sketch, 1.0));
            const Vec3 picked(body.extentRef.point.x, body.extentRef.point.y, body.extentRef.point.z);
            const auto fromPoint = [&](const Vec3 &p) -> ForgeBody {
                const double d = dot(p - origin, e);
                if (std::fabs(d) <= 1e-7) {
                    setError(error, QStringLiteral("Il riferimento sta sul piano dello schizzo: l'estrusione sarebbe nulla."));
                    return nullptr;
                }
                body.distance = d;  // la distanza che ne viene (si vede nella finestra)
                return extrude(d, error);
            };
            if (body.extent == 1) {
                if (!target.hasPoint) {
                    setError(error, QStringLiteral("Il riferimento della fine deve essere un punto."));
                    return nullptr;
                }
                result = fromPoint(target.point);
            } else if (body.extent == 2) {
                Vec3 foot, tangent;
                if (target.hasCurve && target.nearest && target.nearest(picked, foot, tangent)) result = fromPoint(foot);
                else if (target.hasLine) result = fromPoint(target.point + dot(picked - target.point, target.direction) * target.direction);
                else {
                    setError(error, QStringLiteral("Il riferimento della fine deve essere uno spigolo o una curva."));
                    return nullptr;
                }
            } else {
                const bool curvedFace = body.extentRef.kind == 5 && !target.hasPlane;
                // Box del profilo: dall'estrusione di altezza unitaria (esatto, dalle facce).
                const ForgeBody unit = extrude(1.0, error);
                if (!unit) return nullptr;
                const Box profile = bodyBox(*unit);
                const double size = std::max(1.0, profile.diagonal());
                if (!curvedFace) {
                    if (!target.hasPlane) {
                        setError(error, QStringLiteral("Il riferimento della fine deve essere una faccia o un piano."));
                        return nullptr;
                    }
                    const Vec3 n = normalized(target.direction), q = target.point;
                    if (std::fabs(std::fabs(dot(n, e)) - 1.0) <= 1e-12) {
                        result = fromPoint(q);  // piano parallelo allo schizzo: una distanza
                    } else {
                        double lo = 0.0, hi = 0.0;
                        if (!planeReach(profile, origin, e, q, n, lo, hi)) {
                            setError(error, QStringLiteral("Il piano e' parallelo alla direzione dell'estrusione."));
                            return nullptr;
                        }
                        if (lo * hi <= 0.0) {
                            setError(error, QStringLiteral("Il piano attraversa il profilo: l'estrusione andrebbe nei due versi."));
                            return nullptr;
                        }
                        // Prisma oltre il piano, poi solo la parte dalla parte dello schizzo.
                        const double reach = hi > 0.0 ? hi : lo;
                        body.distance = reach;  // il verso (e la portata) per il secondo verso
                        const double length = (reach > 0.0 ? 1.0 : -1.0) * (std::fabs(hi > 0.0 ? hi : lo) * 1.05 + 1e-3 * size);
                        const ForgeBody longer = extrude(length, error);
                        if (!longer) return nullptr;
                        Vec3 z = n;
                        if (dot(origin - q, z) < 0.0) z = -z;
                        const Vec3 center = origin - dot(origin - q, z) * z;
                        const Vec3 x = std::fabs(z.x()) < 0.9 ? Vec3(1, 0, 0) : Vec3(0, 1, 0);
                        const double half = 4.0 * (size + std::fabs(length));
                        const Frame3 boxFrame(center, z, x);
                        const Body box = makeBox(Frame3(boxFrame.toGlobal(Vec3(-half, -half, 0.0)), z, x), 2.0 * half, 2.0 * half, 2.0 * half);
                        const ForgeBody owner = body.extentRef.kind == 5 && body.extentRef.index >= 0 && body.extentRef.index < bodies.size()
                            ? bodies.at(body.extentRef.index).forgeBody : ForgeBody{};
                        result = forgeBooleanWithHistory(longer, extrusionEndTool(box, owner, target.face), Kernel::BooleanOperation::Intersect);
                    }
                } else {
                    // Faccia curva: il prisma fino oltre il corpo della faccia, meno il
                    // corpo (o dentro il corpo); resta la parte che parte dallo schizzo.
                    const int owner = body.extentRef.index;
                    if (owner < 0 || owner >= index || owner >= bodies.size() || !bodies.at(owner).forgeBody) {
                        setError(error, QStringLiteral("Il corpo della faccia non esiste piu'."));
                        return nullptr;
                    }
                    const Body &solid = *bodies.at(owner).forgeBody;
                    const double side = dot(picked - origin, e) >= 0.0 ? 1.0 : -1.0;
                    double reach = 0.0;
                    for (const Vec3 &corner : corners(bodyBox(solid))) reach = std::max(reach, side * dot(corner - origin, e));
                    if (!(reach > 1e-7)) {
                        setError(error, QStringLiteral("La faccia sta dall'altra parte dello schizzo."));
                        return nullptr;
                    }
                    const ForgeBody longer = extrude(side * (reach * 1.05 + 1e-3 * size), error);
                    if (!longer) return nullptr;
                    const auto base = [&](const Body &b, ShellId shell) { return touchesPlane(b, shell, origin, e, 1e-7 * size); };
                    const ForgeBody tool = extrusionEndTool(solid, bodies.at(owner).forgeBody);
                    for (const Kernel::BooleanOperation op : {Kernel::BooleanOperation::Subtract, Kernel::BooleanOperation::Intersect}) {
                        const ForgeBody piece = forgeBooleanWithHistory(longer, tool, op);
                        if (piece->faces().empty()) continue;
                        result = forgeKeepShells(piece, base);
                        if (result) break;
                    }
                    if (!result) {
                        setError(error, QStringLiteral("L'estrusione non arriva alla faccia."));
                        return nullptr;
                    }
                    body.distance = side * reach;
                }
            }
            if (result && body.extrudeSides == 2) {
                // Secondo verso: un prisma dall'altra parte del piano dello
                // schizzo, unito (si toccano lungo la base comune).
                if (!(body.distance2 > 1e-7)) {
                    setError(error, QStringLiteral("La distanza nel secondo verso deve essere positiva."));
                    return nullptr;
                }
                const double s = body.distance > 0.0 ? 1.0 : -1.0;
                const ForgeBody back = forgeExtrusion(sketch, -s * body.distance2, error, 0, body.extrudeSurface, true);
                if (!back) return nullptr;
                if (result->isSheet() || back->isSheet()) {
                    setError(error, QStringLiteral("Una superficie nei due versi si estrude solo a distanza."));
                    return nullptr;
                }
                result = forgeBooleanWithHistory(result, back, Kernel::BooleanOperation::Unite);
            }
        }
    } catch (const std::exception &failure) {
        setError(error, QStringLiteral("Estrusione non riuscita: %1").arg(QString::fromUtf8(failure.what())));
        return nullptr;
    }
    return mergeFeatureResult(body, result, index, bodies, error, disjoint);
}

namespace {

ForgeBody mergeFeatureResult(ExtrusionObject &body, const ForgeBody &result, int index,
                             const QVector<ExtrusionObject> &bodies, QString *error,
                             const DisjointLimits &disjoint) {
    if (!result) return nullptr;
    if (body.mergeOperation == 0) return result;

    // Fusione con gli altri solidi.
    if (result->isSheet()) {
        setError(error, QStringLiteral("Un profilo aperto da' una superficie: non si unisce ne' si sottrae."));
        return nullptr;
    }
    const bool subtract = body.mergeOperation == 2;
    const auto provenDisjoint = [&](int target) {
        if (body.operation >= 0 || body.feature != BodyFeature::Extrusion || body.extent != 4) return false;
        return std::any_of(disjoint.begin(), disjoint.end(), [&](const DisjointLimit &proof) {
            return proof.index == target && target >= 0 && target < bodies.size()
                && proof.geometry == bodies.at(target).forgeBody;
        });
    };
    const auto disjointUnite = [&](const ForgeBody &first, const ForgeBody &second) {
        Kernel::BodyHistory history;
        auto geometry = std::make_shared<const Body>(uniteDisjointBodies(*first, *second, {}, &history));
        registerDeclaredHistory(geometry, {first, second}, std::move(history));
        return geometry;
    };
    QVector<int> targets;
    for (int target : body.mergeBodies) {
        // I bersagli gia' associati sono dipendenze della storyboard. Se
        // falliscono non si puo' restituire soltanto il nuovo prisma come se
        // l'unione fosse riuscita, ne' unire solo gli altri bersagli validi.
        const bool inRange=target>=0&&target<index&&target<bodies.size();
        if(!inRange||!bodies.at(target).forgeBody||bodies.at(target).forgeBody->isSheet()){
            if(body.mergeProbe)continue;  // ricerca dei candidati di una nuova fusione automatica
            const QString name=inRange?bodies.at(target).name:QString::number(target);
            setError(error,QStringLiteral("Il corpo associato alla fusione \"%1\" non ha geometria solida valida.").arg(name));
            return nullptr;
        }
        const ExtrusionObject &other = bodies.at(target);
        if (body.mergeProbe) {
            bool touching = false;
            if (!subtract && provenDisjoint(target)) {
                try {
                    const Body united = uniteDisjointBodies(*result, *other.forgeBody);
                    touching = united.shells().size() < result->shells().size() + other.forgeBody->shells().size();
                } catch (const OperationCancelled &) { throw; }
                catch (const std::exception &failure) {
                    setError(error, QStringLiteral("Contatto per l'unione automatica non risolto: %1").arg(QString::fromUtf8(failure.what())));
                    return nullptr;
                }
            } else touching = forgeSharesMaterial(result, other.forgeBody, subtract);
            if (!touching) continue;
        }
        targets.append(target);
    }
    if (body.mergeProbe) body.mergeBodies = targets;
    if (targets.isEmpty()) {
        if (subtract) {
            setError(error, QStringLiteral("Nessun solido da cui sottrarre la funzione (non ne tocca nessuno)."));
            return nullptr;
        }
        return result;  // unione automatica senza corpi da toccare: un corpo nuovo
    }
    try {
        if (!subtract) {
            if (std::all_of(targets.begin(), targets.end(), provenDisjoint)) {
                // I destinatari possono sovrapporsi tra loro: aggregarli con
                // la booleana generale, poi saldarli al resto gia' disgiunto.
                ForgeBody combinedTargets = bodies.at(targets.front()).forgeBody;
                for (int i = 1; i < targets.size(); ++i)
                    combinedTargets = forgeBooleanWithHistory(combinedTargets, bodies.at(targets[i]).forgeBody, Kernel::BooleanOperation::Unite);
                return disjointUnite(result, combinedTargets);
            }
            ForgeBody united = result;
            for (int target : targets) united = forgeBooleanWithHistory(united, bodies.at(target).forgeBody, Kernel::BooleanOperation::Unite);
            return united;
        }
        ForgeBody cut;
        for (int target : targets) {
            ForgeBody piece = forgeBooleanWithHistory(bodies.at(target).forgeBody, result, Kernel::BooleanOperation::Subtract);
            if (piece->faces().empty()) continue;
            if (!cut) {
                cut = std::move(piece);
                continue;
            }
            // I pezzi dei solidi diversi: uniti, o insieme come pezzi separati se si toccano solo lungo uno spigolo.
            try {
                cut = forgeBooleanWithHistory(cut, piece, Kernel::BooleanOperation::Unite);
            } catch (const std::exception &) {
                BodyHistory history;
                const ForgeBody joined = std::make_shared<const Body>(combined(*cut, *piece, &history));
                registerDeclaredHistory(joined, {cut, piece}, std::move(history));
                cut = joined;
            }
        }
        if (!cut) {
            setError(error, QStringLiteral("La sottrazione toglie tutto il materiale."));
            return nullptr;
        }
        return cut;
    } catch (const std::exception &failure) {
        setError(error, QStringLiteral("Fusione con i corpi non riuscita: %1").arg(QString::fromUtf8(failure.what())));
        return nullptr;
    }
}

} // namespace

ForgeBody forgeMergeFeatureResult(ExtrusionObject &body, const ForgeBody &result, int index,
                                  const QVector<ExtrusionObject> &bodies, QString *error) {
    return mergeFeatureResult(body, result, index, bodies, error, {});
}

void forgeComponentCounts(const ForgeBody &body, int &solids, int &sheets) {
    solids = sheets = 0;
    if (!body) return;
    const std::vector<ShellId> shells = body->shells();
    if (body->isSheet()) {
        sheets = int(shells.size());
        return;
    }
    if (shells.size() <= 1) {
        solids = int(shells.size());
        return;
    }
    // Un punto di ogni shell: se sta dentro un'altra shell, la shell e' una cavita'.
    std::vector<Vec3> points;
    for (ShellId shell : shells) {
        Vec3 point;
        bool found = false;
        for (FaceId f : body->shell(shell).faces) {
            for (LoopId l : body->face(f).loops)
                if (body->loop(l).first.valid()) {
                    point = body->vertex(body->fin(body->loop(l).first).vertex).point;
                    found = true;
                    break;
                }
            if (!found) {
                const Surface &surface = *body->face(f).surface;
                const Interval u = surface.uDomain(), v = surface.vDomain();
                Vec3 out;
                surface.evaluate(std::isfinite(u.lo + u.hi) ? 0.5 * (u.lo + u.hi) : 0.0, std::isfinite(v.lo + v.hi) ? 0.5 * (v.lo + v.hi) : 0.0, 0, &out);
                point = out;
                found = true;
            }
            if (found) break;
        }
        points.push_back(point);
    }
    // Una shell puo' stare dentro un'altra solo se il suo box sta nel box dell'altra.
    std::vector<Box> boxes;
    for (ShellId shell : shells) {
        Box box;
        for (FaceId f : body->shell(shell).faces) box.add(faceBox(*body, f));
        boxes.push_back(box);
    }
    const auto inside = [](const Box &a, const Box &b) {
        for (int k = 0; k < 3; ++k)
            if (a.lo[k] < b.lo[k] - 1e-9 || a.hi[k] > b.hi[k] + 1e-9) return false;
        return true;
    };
    std::vector<ForgeBody> single(shells.size());
    for (std::size_t a = 0; a < shells.size(); ++a) {
        bool cavity = false;
        for (std::size_t b = 0; b < shells.size() && !cavity; ++b) {
            if (a == b || !inside(boxes[a], boxes[b])) continue;
            if (!single[b]) {
                const ShellId shell = shells[b];
                single[b] = forgeKeepShells(body, [shell](const Body &, ShellId s) { return s == shell; });
            }
            if (!single[b]) continue;
            try {
                cavity = SolidClassifier(*single[b], 1e-7).classify(points[a]) == PointLocation::Inside;
            } catch (const std::exception &) {
            }
        }
        if (!cavity) ++solids;
    }
}

}
