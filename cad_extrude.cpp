#include "cad_extrude.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <map>
#include <memory>

#include "cad_datum.h"
#include "cad_forge.h"
#include "cad_kernel.h"
#include "fk_boolean.h"
#include "fk_classify.h"
#include "fk_intersect.h"
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
                  std::vector<Body::BuildEdge> &edges, std::vector<Body::BuildFace> &faces) {
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
        }
    }
}

// Due solidi in un body solo senza booleana (pezzi separati, anche se si
// toccano lungo uno spigolo): quando l'unione non si puo' fare.
Body combined(const Body &a, const Body &b) {
    std::vector<Vec3> vertices;
    std::vector<Body::BuildEdge> edges;
    std::vector<Body::BuildFace> faces;
    const auto all = [](const Body &, ShellId) { return true; };
    appendShells(a, all, vertices, edges, faces);
    appendShells(b, all, vertices, edges, faces);
    return Body::build(vertices, edges, faces);
}

}

ForgeBody forgeKeepShells(const ForgeBody &body, const std::function<bool(const Body &, ShellId)> &keep) {
    if (!body) return nullptr;
    std::vector<Vec3> vertices;
    std::vector<Body::BuildEdge> edges;
    std::vector<Body::BuildFace> faces;
    appendShells(*body, keep, vertices, edges, faces);
    if (faces.empty()) return nullptr;
    return std::make_shared<const Body>(Body::build(vertices, edges, faces));
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

ForgeBody forgeExtrusionFeature(ExtrusionObject &body, int index, const QVector<SketchObject> &sketches, const QVector<ExtrusionObject> &bodies,
                                QString *error) {
    if (body.sketchIndex < 0 || body.sketchIndex >= sketches.size()) {
        setError(error, QStringLiteral("Lo schizzo del corpo non esiste piu'."));
        return nullptr;
    }
    const SketchObject &sketch = sketches.at(body.sketchIndex);
    ForgeBody result;
    try {
        if (body.extent == 0) {
            result = forgeExtrusion(sketch, body.distance, error);
        } else {
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
                return forgeExtrusion(sketch, d, error);
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
                const ForgeBody unit = forgeExtrusion(sketch, 1.0, error);
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
                        const double length = (reach > 0.0 ? 1.0 : -1.0) * (std::fabs(hi > 0.0 ? hi : lo) * 1.05 + 1e-3 * size);
                        const ForgeBody longer = forgeExtrusion(sketch, length, error);
                        if (!longer) return nullptr;
                        Vec3 z = n;
                        if (dot(origin - q, z) < 0.0) z = -z;
                        const Vec3 center = origin - dot(origin - q, z) * z;
                        const Vec3 x = std::fabs(z.x()) < 0.9 ? Vec3(1, 0, 0) : Vec3(0, 1, 0);
                        const double half = 4.0 * (size + std::fabs(length));
                        const Frame3 boxFrame(center, z, x);
                        const Body box = makeBox(Frame3(boxFrame.toGlobal(Vec3(-half, -half, 0.0)), z, x), 2.0 * half, 2.0 * half, 2.0 * half);
                        result = std::make_shared<const Body>(booleanOperation(*longer, box, Kernel::BooleanOperation::Intersect));
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
                    const ForgeBody longer = forgeExtrusion(sketch, side * (reach * 1.05 + 1e-3 * size), error);
                    if (!longer) return nullptr;
                    const auto base = [&](const Body &b, ShellId shell) { return touchesPlane(b, shell, origin, e, 1e-7 * size); };
                    for (const Kernel::BooleanOperation op : {Kernel::BooleanOperation::Subtract, Kernel::BooleanOperation::Intersect}) {
                        const Body piece = booleanOperation(*longer, solid, op);
                        if (piece.faces().empty()) continue;
                        result = forgeKeepShells(std::make_shared<const Body>(piece), base);
                        if (result) break;
                    }
                    if (!result) {
                        setError(error, QStringLiteral("L'estrusione non arriva alla faccia."));
                        return nullptr;
                    }
                    body.distance = side * reach;
                }
            }
        }
    } catch (const std::exception &failure) {
        setError(error, QStringLiteral("Estrusione non riuscita: %1").arg(QString::fromUtf8(failure.what())));
        return nullptr;
    }
    return forgeMergeFeatureResult(body, result, index, bodies, error);
}

ForgeBody forgeMergeFeatureResult(ExtrusionObject &body, const ForgeBody &result, int index,
                                  const QVector<ExtrusionObject> &bodies, QString *error) {
    if (!result) return nullptr;
    if (body.mergeOperation == 0) return result;

    // Fusione con gli altri solidi.
    if (result->isSheet()) {
        setError(error, QStringLiteral("Un profilo aperto da' una superficie: non si unisce ne' si sottrae."));
        return nullptr;
    }
    const bool subtract = body.mergeOperation == 2;
    QVector<int> targets;
    for (int target : body.mergeBodies) {
        if (target < 0 || target >= index || target >= bodies.size()) continue;
        const ExtrusionObject &other = bodies.at(target);
        if (!other.forgeBody || other.forgeBody->isSheet()) continue;
        if (body.mergeProbe && !forgeSharesMaterial(result, other.forgeBody, subtract)) continue;
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
            Body united = *result;
            for (int target : targets) united = booleanOperation(united, *bodies.at(target).forgeBody, Kernel::BooleanOperation::Unite);
            return std::make_shared<const Body>(std::move(united));
        }
        std::unique_ptr<Body> cut;
        for (int target : targets) {
            Body piece = booleanOperation(*bodies.at(target).forgeBody, *result, Kernel::BooleanOperation::Subtract);
            if (piece.faces().empty()) continue;
            if (!cut) {
                cut = std::make_unique<Body>(std::move(piece));
                continue;
            }
            // I pezzi dei solidi diversi: uniti, o insieme come pezzi separati se si toccano solo lungo uno spigolo.
            try {
                cut = std::make_unique<Body>(booleanOperation(*cut, piece, Kernel::BooleanOperation::Unite));
            } catch (const std::exception &) {
                cut = std::make_unique<Body>(combined(*cut, piece));
            }
        }
        if (!cut) {
            setError(error, QStringLiteral("La sottrazione toglie tutto il materiale."));
            return nullptr;
        }
        return std::make_shared<const Body>(std::move(*cut));
    } catch (const std::exception &failure) {
        setError(error, QStringLiteral("Fusione con i corpi non riuscita: %1").arg(QString::fromUtf8(failure.what())));
        return nullptr;
    }
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
