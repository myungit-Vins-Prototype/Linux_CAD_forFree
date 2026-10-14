#include "fk_history.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <exception>
#include <limits>
#include <set>
#include <stdexcept>

#include "fk_classify.h"
#include "fk_curve_algo.h"
#include "fk_intersect.h"
#include "fk_surface_algo.h"

namespace ForgeCad::Kernel {

// --- BodyHistory ---------------------------------------------------------------

BodyHistory::BodyHistory(int operandCount, const std::vector<int> &faceCounts, const std::vector<int> &edgeCounts,
                         const std::vector<int> &vertexCounts, int resultFaces, int resultEdges, int resultVertices)
    : operands_(operandCount) {
    if (int(faceCounts.size()) != operandCount || int(edgeCounts.size()) != operandCount || int(vertexCounts.size()) != operandCount)
        throw std::invalid_argument("BodyHistory: conteggi degli operandi incoerenti");
    sizes_[int(EntityKind::Face)] = faceCounts;
    sizes_[int(EntityKind::Edge)] = edgeCounts;
    sizes_[int(EntityKind::Vertex)] = vertexCounts;
    links_[int(EntityKind::Face)].resize(std::size_t(std::max(0, resultFaces)));
    links_[int(EntityKind::Edge)].resize(std::size_t(std::max(0, resultEdges)));
    links_[int(EntityKind::Vertex)].resize(std::size_t(std::max(0, resultVertices)));
}

int BodyHistory::operandSize(int operand, EntityKind kind) const {
    const auto &sizes = sizes_[int(kind)];
    return operand >= 0 && operand < int(sizes.size()) ? sizes[std::size_t(operand)] : 0;
}

const std::vector<HistoryLink> &BodyHistory::origins(EntityKind kind, int index) const {
    static const std::vector<HistoryLink> none;
    const auto &links = links_[int(kind)];
    return index >= 0 && index < int(links.size()) ? links[std::size_t(index)] : none;
}

void BodyHistory::addOrigin(EntityKind kind, int index, const HistoryLink &link) {
    auto &links = links_[int(kind)];
    if (index < 0 || index >= int(links.size())) throw std::out_of_range("BodyHistory::addOrigin: indice non valido");
    auto &list = links[std::size_t(index)];
    for (HistoryLink &existing : list)
        if (existing.operand == link.operand && existing.kind == link.kind && existing.index == link.index) {
            existing.relation = std::min(existing.relation, link.relation);
            return;
        }
    list.push_back(link);
}

std::vector<int> BodyHistory::descendants(EntityKind kind, int operand, int index) const {
    std::vector<int> result;
    const auto &links = links_[int(kind)];
    for (std::size_t r = 0; r < links.size(); ++r)
        for (const HistoryLink &link : links[r])
            if (link.operand == operand && link.kind == kind && link.index == index && link.relation != HistoryRelation::Generated) {
                result.push_back(int(r));
                break;
            }
    return result;
}

std::vector<int> BodyHistory::generated(EntityKind resultKind, EntityKind kind, int operand, int index) const {
    std::vector<int> result;
    const auto &links = links_[int(resultKind)];
    for (std::size_t r = 0; r < links.size(); ++r)
        for (const HistoryLink &link : links[r])
            if (link.operand == operand && link.kind == kind && link.index == index && link.relation == HistoryRelation::Generated) {
                result.push_back(int(r));
                break;
            }
    return result;
}

Evolution BodyHistory::evolution(EntityKind kind, int operand, int index) const {
    const std::vector<int> found = descendants(kind, operand, index);
    if (found.empty()) return Evolution::Deleted;
    if (found.size() > 1) return Evolution::Split;
    int sources = 0;
    HistoryRelation relation = HistoryRelation::Same;
    for (const HistoryLink &link : origins(kind, found.front()))
        if (link.kind == kind && link.relation != HistoryRelation::Generated) {
            ++sources;
            if (link.operand == operand && link.index == index) relation = link.relation;
        }
    if (sources > 1) return Evolution::Merged;
    return relation == HistoryRelation::Same ? Evolution::Unchanged : Evolution::Modified;
}

BodyHistory BodyHistory::compose(const BodyHistory &first, const BodyHistory &second, int secondOperand) {
    if (secondOperand < 0 || secondOperand >= second.operands_) throw std::invalid_argument("BodyHistory::compose: operando non valido");
    const int operands = first.operands_ + second.operands_ - 1;
    std::vector<int> sizes[3];
    for (int k = 0; k < 3; ++k) {
        sizes[k] = first.sizes_[k];
        for (int o = 0; o < second.operands_; ++o)
            if (o != secondOperand) sizes[k].push_back(second.sizes_[k][std::size_t(o)]);
    }
    BodyHistory result(operands, sizes[int(EntityKind::Face)], sizes[int(EntityKind::Edge)], sizes[int(EntityKind::Vertex)],
                       second.resultSize(EntityKind::Face), second.resultSize(EntityKind::Edge), second.resultSize(EntityKind::Vertex));
    const auto combine = [](HistoryRelation a, HistoryRelation b) {
        if (a == HistoryRelation::Generated || b == HistoryRelation::Generated) return HistoryRelation::Generated;
        if (a == HistoryRelation::Same && b == HistoryRelation::Same) return HistoryRelation::Same;
        return HistoryRelation::Modified;
    };
    for (int k = 0; k < 3; ++k) {
        const EntityKind kind = EntityKind(k);
        for (int r = 0; r < second.resultSize(kind); ++r)
            for (const HistoryLink &link : second.origins(kind, r)) {
                if (link.operand != secondOperand) {
                    HistoryLink mapped = link;
                    mapped.operand = first.operands_ + (link.operand > secondOperand ? link.operand - 1 : link.operand);
                    result.addOrigin(kind, r, mapped);
                    continue;
                }
                for (const HistoryLink &inner : first.origins(link.kind, link.index)) {
                    HistoryLink mapped = inner;
                    mapped.relation = combine(inner.relation, link.relation);
                    result.addOrigin(kind, r, mapped);
                }
            }
    }
    return result;
}

// --- Punto interno di una faccia -------------------------------------------------

namespace {

double faceBoundaryTolerance(const Body &body, FaceId face) {
    double tolerance = 0.0;
    for (LoopId loop : body.face(face).loops)
        for (FinId fin : body.loopFins(loop)) tolerance = std::max(tolerance, body.edge(body.fin(fin).edge).tolerance);
    return tolerance;
}

double edgeScale(const Edge &edge) {
    const double lo = edge.range.lo, hi = edge.range.hi;
    if (!edge.curve || !std::isfinite(lo) || !std::isfinite(hi)) return 1.0;
    const Vec3 a = edge.curve->point(lo), b = edge.curve->point(hi), m = edge.curve->point(0.5 * (lo + hi));
    return std::max({distance(a, b), distance(a, m) + distance(m, b), 1e-6});
}

}

bool faceInteriorPoint(const Body &body, FaceId face, Vec3 &point, Vec2 *uvOut) {
    const Face &data = body.face(face);
    if (!data.surface) return false;
    const Surface &surface = *data.surface;
    const double boundary = 1e-9 + faceBoundaryTolerance(body, face);
    std::vector<FinId> fins;
    for (LoopId loop : data.loops)
        if (body.loop(loop).first.valid())
            for (FinId fin : body.loopFins(loop)) fins.push_back(fin);
    if (fins.empty()) {
        // Faccia senza bordo (sfera, toro interi): un punto qualsiasi.
        const Interval u = surface.uDomain(), v = surface.vDomain();
        const double uu = u.isFinite() ? u.lo + 0.37 * u.length() : 0.37, vv = v.isFinite() ? v.lo + 0.41 * v.length() : 0.41;
        point = surface.point(uu, vv);
        if (uvOut) *uvOut = Vec2(uu, vv);
        return true;
    }
    for (double fraction : {0.5, 0.31, 0.69, 0.13, 0.87}) {
        for (FinId f : fins) {
            const Fin &fin = body.fin(f);
            if (!fin.pcurve) continue;
            const Edge &edge = body.edge(fin.edge);
            if (!std::isfinite(edge.range.lo) || !std::isfinite(edge.range.hi)) continue;
            const double t = edge.range.lo + fraction * (edge.range.hi - edge.range.lo);
            Vec2 jet[2];
            try {
                fin.pcurve->evaluate(t, 1, jet);
            } catch (const std::exception &) {
                continue;
            }
            Vec2 along = jet[1] * (fin.sense ? 1.0 : -1.0);
            if (!(norm(along) > 0.0)) continue;
            // La faccia sta a sinistra della fin rispetto alla sua normale;
            // nello spazio (u, v) la normale della superficie e' Su x Sv.
            Vec2 left(-along.y(), along.x());
            if (!data.sense) left = -left;
            left = left / norm(left);
            Vec3 derivatives[4];
            surface.evaluate(jet[0].x(), jet[0].y(), 1, derivatives);
            const Vec3 su = derivatives[Surface::derivativeIndex(1, 0, 1)], sv = derivatives[Surface::derivativeIndex(0, 1, 1)];
            const double speed = norm(su * left.x() + sv * left.y());
            if (!(speed > 0.0) || !std::isfinite(speed)) continue;
            const double scale = edgeScale(edge);
            for (double step : {1e-2, 3e-3, 1e-3, 3e-2, 1e-4, 1e-5}) {
                const Vec2 uv = jet[0] + left * (step * scale / speed);
                const Vec3 p = surface.point(uv.x(), uv.y());
                if (!std::isfinite(p.x()) || !std::isfinite(p.y()) || !std::isfinite(p.z())) continue;
                try {
                    if (classifyPointOnFace(body, face, p, boundary) == PointLocation::Inside) {
                        point = p;
                        if (uvOut) *uvOut = uv;
                        return true;
                    }
                } catch (const std::exception &) {
                }
            }
        }
    }
    return false;
}

// --- Inferenza della genealogia -------------------------------------------------

namespace {

struct BodyCache {
    const Body *body = nullptr;
    std::vector<FaceId> faces;
    std::vector<EdgeId> edges;
    std::vector<VertexId> vertices;
    std::vector<Box> faceBoxes, edgeBoxes;
    std::vector<int> pointState;  // 0 non calcolato, 1 trovato, -1 non trovato
    std::vector<Vec3> facePoints;
    std::vector<std::array<Vec3, 5>> edgeSamples;  // inizio, 1/4, 1/2, 3/4, fine

    explicit BodyCache(const Body *b) : body(b) {
        if (!body) return;
        faces = body->faces();
        edges = body->edges();
        vertices = body->vertices();
        faceBoxes.resize(std::size_t(body->counts().faces) + 64);
        std::size_t faceSlots = 0, edgeSlots = 0;
        for (FaceId f : faces) faceSlots = std::max(faceSlots, std::size_t(f.index) + 1);
        for (EdgeId e : edges) edgeSlots = std::max(edgeSlots, std::size_t(e.index) + 1);
        faceBoxes.assign(faceSlots, Box());
        pointState.assign(faceSlots, 0);
        facePoints.assign(faceSlots, Vec3());
        edgeBoxes.assign(edgeSlots, Box());
        edgeSamples.assign(edgeSlots, {});
        for (FaceId f : faces) {
            try {
                faceBoxes[std::size_t(f.index)] = faceBox(*body, f);
            } catch (const std::exception &) {
                Box box;
                for (LoopId loop : body->face(f).loops)
                    for (FinId fin : body->loopFins(loop)) box.add(body->vertex(body->fin(fin).vertex).point);
                faceBoxes[std::size_t(f.index)] = box;
            }
        }
        for (EdgeId e : edges) {
            const Edge &edge = body->edge(e);
            auto &samples = edgeSamples[std::size_t(e.index)];
            Box box;
            if (edge.curve && std::isfinite(edge.range.lo) && std::isfinite(edge.range.hi)) {
                for (int k = 0; k < 5; ++k) samples[std::size_t(k)] = edge.curve->point(edge.range.lo + 0.25 * k * (edge.range.hi - edge.range.lo));
                try {
                    box = curveBox(*edge.curve, edge.range);
                } catch (const std::exception &) {
                    for (const Vec3 &p : samples) box.add(p);
                }
            }
            edgeBoxes[std::size_t(e.index)] = box;
        }
    }

    bool interior(FaceId face, Vec3 &point) {
        int &state = pointState[std::size_t(face.index)];
        if (state == 0) state = faceInteriorPoint(*body, face, facePoints[std::size_t(face.index)]) ? 1 : -1;
        point = facePoints[std::size_t(face.index)];
        return state > 0;
    }
};

bool surfacesMatch(const Surface &a, const Surface &b, double tolerance) {
    if (&a == &b) return true;
    if (a.type() != b.type()) return false;
    try {
        return sameSurface(a, b, tolerance);
    } catch (const std::exception &) {
        return false;
    }
}

// Il punto sta sulla faccia (dentro o sul bordo).
bool pointOnFace(const Body &body, FaceId face, const Vec3 &point, double tolerance) {
    try {
        return classifyPointOnFace(body, face, point, tolerance) != PointLocation::Outside;
    } catch (const std::exception &) {
        return false;
    }
}

double distanceToEdge(const Edge &edge, const Vec3 &point) {
    if (!edge.curve) return std::numeric_limits<double>::infinity();
    try {
        return projectPoint(*edge.curve, point, edge.range).distance;
    } catch (const std::exception &) {
        return std::numeric_limits<double>::infinity();
    }
}

std::vector<Vec3> faceVertexPoints(const Body &body, FaceId face) {
    std::vector<Vec3> points;
    for (LoopId loop : body.face(face).loops) {
        const Loop &data = body.loop(loop);
        if (!data.first.valid()) continue;
        for (FinId fin : body.loopFins(loop)) points.push_back(body.vertex(body.fin(fin).vertex).point);
    }
    return points;
}

int finCount(const Body &body, FaceId face) {
    int count = 0;
    for (LoopId loop : body.face(face).loops)
        if (body.loop(loop).first.valid()) count += int(body.loopFins(loop).size());
    return count;
}

bool samePointSets(const std::vector<Vec3> &a, const std::vector<Vec3> &b, double tolerance) {
    if (a.size() != b.size()) return false;
    for (const Vec3 &p : a) {
        bool found = false;
        for (const Vec3 &q : b)
            if (distance(p, q) <= tolerance) {
                found = true;
                break;
            }
        if (!found) return false;
    }
    return true;
}

}

BodyHistory inferHistory(const std::vector<const Body *> &operands, const Body &result, const HistoryOptions &options) {
    const double tol = std::max(options.tolerance, 0.0);
    std::vector<BodyCache> caches;
    caches.reserve(operands.size());
    std::vector<int> faceCounts, edgeCounts, vertexCounts;
    for (const Body *operand : operands) {
        caches.emplace_back(operand);
        faceCounts.push_back(operand ? int(caches.back().faceBoxes.size()) : 0);
        edgeCounts.push_back(operand ? int(caches.back().edgeBoxes.size()) : 0);
        int vertexSlots = 0;
        if (operand)
            for (VertexId v : operand->vertices()) vertexSlots = std::max(vertexSlots, v.index + 1);
        vertexCounts.push_back(vertexSlots);
    }
    BodyCache own(&result);
    int resultVertexSlots = 0;
    for (VertexId v : result.vertices()) resultVertexSlots = std::max(resultVertexSlots, v.index + 1);
    BodyHistory history(int(operands.size()), faceCounts, edgeCounts, vertexCounts, int(own.faceBoxes.size()), int(own.edgeBoxes.size()),
                        resultVertexSlots);

    // Facce: stessa superficie e punto interno di una dentro l'altra.
    for (FaceId r : own.faces) {
        const Face &rf = result.face(r);
        if (!rf.surface) continue;
        const Box rBox = own.faceBoxes[std::size_t(r.index)];
        const double rTolerance = tol + faceBoundaryTolerance(result, r);
        Vec3 rPoint;
        const bool rInterior = own.interior(r, rPoint);
        for (std::size_t o = 0; o < caches.size(); ++o) {
            BodyCache &cache = caches[o];
            if (!cache.body) continue;
            for (FaceId f : cache.faces) {
                const Face &of = cache.body->face(f);
                if (!of.surface) continue;
                const bool identical = of.surface.get() == rf.surface.get();
                const double fTolerance = tol + faceBoundaryTolerance(*cache.body, f);
                const double t = rTolerance + fTolerance;
                if (!identical) {
                    if (!rBox.padded(t).overlaps(cache.faceBoxes[std::size_t(f.index)])) continue;
                    if (!surfacesMatch(*rf.surface, *of.surface, t)) continue;
                }
                bool linked = rInterior && pointOnFace(*cache.body, f, rPoint, t);
                Vec3 fPoint;
                if (!linked && cache.interior(f, fPoint)) linked = pointOnFace(result, r, fPoint, t);
                if (!linked) continue;
                const bool same = finCount(result, r) == finCount(*cache.body, f) && rf.loops.size() == of.loops.size()
                               && samePointSets(faceVertexPoints(result, r), faceVertexPoints(*cache.body, f), t);
                history.addOrigin(EntityKind::Face, r.index,
                                  {int(o), EntityKind::Face, f.index, same ? HistoryRelation::Same : HistoryRelation::Modified});
            }
        }
    }

    // Edge: tre punti interni di uno sull'altro (in un verso o nell'altro).
    for (EdgeId r : own.edges) {
        const Edge &re = result.edge(r);
        if (!re.curve) continue;
        const Box rBox = own.edgeBoxes[std::size_t(r.index)];
        const auto &rSamples = own.edgeSamples[std::size_t(r.index)];
        for (std::size_t o = 0; o < caches.size(); ++o) {
            BodyCache &cache = caches[o];
            if (!cache.body) continue;
            for (EdgeId e : cache.edges) {
                const Edge &oe = cache.body->edge(e);
                if (!oe.curve) continue;
                const double t = 2.0 * tol + re.tolerance + oe.tolerance;
                if (oe.curve.get() != re.curve.get() && !rBox.padded(t).overlaps(cache.edgeBoxes[std::size_t(e.index)])) continue;
                const auto &oSamples = cache.edgeSamples[std::size_t(e.index)];
                const auto inside = [&](const std::array<Vec3, 5> &samples, const Edge &on) {
                    for (int k = 1; k <= 3; ++k)
                        if (!(distanceToEdge(on, samples[std::size_t(k)]) <= t)) return false;
                    return true;
                };
                const bool rInO = inside(rSamples, oe);
                const bool oInR = inside(oSamples, re);
                if (!rInO && !oInR) continue;
                const bool sameEnds = (distance(rSamples[0], oSamples[0]) <= t && distance(rSamples[4], oSamples[4]) <= t)
                                   || (distance(rSamples[0], oSamples[4]) <= t && distance(rSamples[4], oSamples[0]) <= t);
                const bool same = rInO && oInR && sameEnds;
                history.addOrigin(EntityKind::Edge, r.index,
                                  {int(o), EntityKind::Edge, e.index, same ? HistoryRelation::Same : HistoryRelation::Modified});
            }
        }
    }

    // Vertici: stesso punto.
    for (VertexId r : own.vertices) {
        const Vertex &rv = result.vertex(r);
        for (std::size_t o = 0; o < caches.size(); ++o) {
            const BodyCache &cache = caches[o];
            if (!cache.body) continue;
            for (VertexId v : cache.vertices) {
                const Vertex &ov = cache.body->vertex(v);
                if (distance(rv.point, ov.point) <= 2.0 * tol + rv.tolerance + ov.tolerance)
                    history.addOrigin(EntityKind::Vertex, r.index, {int(o), EntityKind::Vertex, v.index, HistoryRelation::Same});
            }
        }
    }

    if (!options.generatedOrigins) return history;

    const auto hasDescendant = [&](EntityKind kind, int operand, int index) { return !history.descendants(kind, operand, index).empty(); };
    const auto inherited = [&](EntityKind kind, int index) {
        for (const HistoryLink &link : history.origins(kind, index))
            if (link.relation != HistoryRelation::Generated && link.kind == kind) return true;
        return false;
    };

    // Facce nuove: generate dagli spigoli scomparsi tra le facce da cui
    // derivano le facce vicine (raccordi, smussi), o dai vertici scomparsi
    // comuni a quegli spigoli (pezze d'angolo).
    std::vector<FaceId> fresh;
    for (FaceId r : own.faces)
        if (!inherited(EntityKind::Face, r.index)) fresh.push_back(r);
    std::vector<std::vector<std::pair<int, int>>> freshEdges(own.faceBoxes.size());  // faccia nuova -> (operando, edge)
    for (FaceId r : fresh) {
        std::vector<FaceId> neighbors;
        for (LoopId loop : result.face(r).loops) {
            if (!result.loop(loop).first.valid()) continue;
            for (FinId fin : result.loopFins(loop)) {
                const FinId other = result.otherFin(fin);
                if (other.valid()) neighbors.push_back(result.finFace(other));
            }
        }
        std::vector<std::set<int>> ancestors(caches.size());
        for (FaceId n : neighbors)
            for (const HistoryLink &link : history.origins(EntityKind::Face, n.index))
                if (link.kind == EntityKind::Face && link.relation != HistoryRelation::Generated)
                    ancestors[std::size_t(link.operand)].insert(link.index);
        for (std::size_t o = 0; o < caches.size(); ++o) {
            const BodyCache &cache = caches[o];
            if (!cache.body || ancestors[o].empty()) continue;
            for (EdgeId e : cache.edges) {
                const Edge &oe = cache.body->edge(e);
                if (!oe.forward.valid() || !oe.backward.valid()) continue;
                const int a = cache.body->finFace(oe.forward).index, b = cache.body->finFace(oe.backward).index;
                if (!ancestors[o].count(a) || !ancestors[o].count(b)) continue;
                if (hasDescendant(EntityKind::Edge, int(o), e.index)) continue;
                history.addOrigin(EntityKind::Face, r.index, {int(o), EntityKind::Edge, e.index, HistoryRelation::Generated});
                freshEdges[std::size_t(r.index)].push_back({int(o), e.index});
            }
        }
    }
    for (FaceId r : fresh) {
        if (!history.origins(EntityKind::Face, r.index).empty()) continue;
        // Spigoli generatori delle facce nuove vicine: il vertice comune a
        // due di loro, scomparso, genera questa faccia.
        std::vector<std::pair<int, int>> around;
        for (LoopId loop : result.face(r).loops) {
            if (!result.loop(loop).first.valid()) continue;
            for (FinId fin : result.loopFins(loop)) {
                const FinId other = result.otherFin(fin);
                if (!other.valid()) continue;
                for (const auto &edge : freshEdges[std::size_t(result.finFace(other).index)]) around.push_back(edge);
            }
        }
        for (std::size_t o = 0; o < caches.size(); ++o) {
            const BodyCache &cache = caches[o];
            if (!cache.body) continue;
            for (VertexId v : cache.vertices) {
                if (hasDescendant(EntityKind::Vertex, int(o), v.index)) continue;
                int touching = 0;
                for (const auto &[operand, edge] : around)
                    if (operand == int(o)) {
                        const EdgeId id(edge);
                        if (cache.body->edgeStart(id) == v || cache.body->edgeEnd(id) == v) ++touching;
                    }
                if (touching >= 2) history.addOrigin(EntityKind::Face, r.index, {int(o), EntityKind::Vertex, v.index, HistoryRelation::Generated});
            }
        }
    }

    // Edge nuovi: le facce degli operandi su cui giacciono.
    for (EdgeId r : own.edges) {
        if (inherited(EntityKind::Edge, r.index)) continue;
        const Edge &re = result.edge(r);
        const Vec3 middle = own.edgeSamples[std::size_t(r.index)][2];
        for (std::size_t o = 0; o < caches.size(); ++o) {
            const BodyCache &cache = caches[o];
            if (!cache.body) continue;
            for (FaceId f : cache.faces) {
                const double t = 2.0 * tol + re.tolerance + faceBoundaryTolerance(*cache.body, f);
                if (!cache.faceBoxes[std::size_t(f.index)].padded(t).overlaps(own.edgeBoxes[std::size_t(r.index)])) continue;
                try {
                    const SurfaceProjection projection = projectPoint(*cache.body->face(f).surface, middle);
                    if (projection.distance <= t && pointOnFace(*cache.body, f, projection.point, t))
                        history.addOrigin(EntityKind::Edge, r.index, {int(o), EntityKind::Face, f.index, HistoryRelation::Generated});
                } catch (const std::exception &) {
                }
            }
        }
    }

    // Vertici nuovi: gli edge degli operandi su cui giacciono.
    for (VertexId r : own.vertices) {
        if (inherited(EntityKind::Vertex, r.index)) continue;
        const Vertex &rv = result.vertex(r);
        for (std::size_t o = 0; o < caches.size(); ++o) {
            const BodyCache &cache = caches[o];
            if (!cache.body) continue;
            for (EdgeId e : cache.edges) {
                const Edge &oe = cache.body->edge(e);
                const double t = 2.0 * tol + rv.tolerance + oe.tolerance;
                Box box;
                box.add(rv.point);
                if (!box.padded(t).overlaps(cache.edgeBoxes[std::size_t(e.index)])) continue;
                if (distanceToEdge(oe, rv.point) <= t)
                    history.addOrigin(EntityKind::Vertex, r.index, {int(o), EntityKind::Edge, e.index, HistoryRelation::Generated});
            }
        }
    }
    return history;
}

BodyHistory sameIndexHistory(const Body &operand, const Body &result) {
    const auto slots = [](const auto &ids) {
        int count = 0;
        for (const auto &id : ids) count = std::max(count, id.index + 1);
        return count;
    };
    const int faces = slots(operand.faces()), edges = slots(operand.edges()), vertices = slots(operand.vertices());
    if (faces != slots(result.faces()) || edges != slots(result.edges()) || vertices != slots(result.vertices()))
        throw std::invalid_argument("sameIndexHistory: topologie diverse");
    BodyHistory history(1, {faces}, {edges}, {vertices}, faces, edges, vertices);
    for (FaceId f : result.faces())
        if (operand.contains(f)) history.addOrigin(EntityKind::Face, f.index, {0, EntityKind::Face, f.index, HistoryRelation::Modified});
    for (EdgeId e : result.edges())
        if (operand.contains(e)) history.addOrigin(EntityKind::Edge, e.index, {0, EntityKind::Edge, e.index, HistoryRelation::Modified});
    for (VertexId v : result.vertices())
        if (operand.contains(v)) history.addOrigin(EntityKind::Vertex, v.index, {0, EntityKind::Vertex, v.index, HistoryRelation::Modified});
    return history;
}

// --- Nomi ---------------------------------------------------------------------

const std::vector<std::vector<std::string>> &ElementNames::table(EntityKind kind) const {
    return kind == EntityKind::Face ? faces : kind == EntityKind::Edge ? edges : vertices;
}

std::vector<std::vector<std::string>> &ElementNames::table(EntityKind kind) {
    return kind == EntityKind::Face ? faces : kind == EntityKind::Edge ? edges : vertices;
}

std::string ElementNames::primary(EntityKind kind, int index) const {
    const auto &names = table(kind);
    return index >= 0 && index < int(names.size()) && !names[std::size_t(index)].empty() ? names[std::size_t(index)].front() : std::string();
}

std::vector<int> ElementNames::find(EntityKind kind, const std::string &name) const {
    std::vector<int> found;
    const auto &names = table(kind);
    for (std::size_t i = 0; i < names.size(); ++i)
        if (std::binary_search(names[i].begin(), names[i].end(), name)) found.push_back(int(i));
    return found;
}

namespace {

std::string compact(const std::string &tag, const std::string &name) {
    constexpr std::size_t kMaxName = 160;
    if (name.size() <= kMaxName) return name;
    std::uint64_t hash = 1469598103934665603ull;
    for (unsigned char c : name) {
        hash ^= c;
        hash *= 1099511628211ull;
    }
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%016llx", static_cast<unsigned long long>(hash));
    return tag + ":H" + buffer;
}

void normalize(std::vector<std::string> &names) {
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
}

std::string joined(std::vector<std::string> parts, const char *separator) {
    normalize(parts);
    std::string text;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i) text += separator;
        text += parts[i];
    }
    return text;
}

}

ElementNames propagateNames(const Body &result, const BodyHistory &history, const std::vector<const ElementNames *> &operandNames,
                            const std::string &tag, const FaceLabels *labels) {
    ElementNames names;
    names.body = &result;
    const auto slots = [](const auto &ids) {
        int count = 0;
        for (const auto &id : ids) count = std::max(count, id.index + 1);
        return count;
    };
    names.faces.resize(std::size_t(slots(result.faces())));
    names.edges.resize(std::size_t(slots(result.edges())));
    names.vertices.resize(std::size_t(slots(result.vertices())));
    const auto operandName = [&](const HistoryLink &link) -> const std::vector<std::string> * {
        if (link.operand < 0 || link.operand >= int(operandNames.size()) || !operandNames[std::size_t(link.operand)]) return nullptr;
        const auto &table = operandNames[std::size_t(link.operand)]->table(link.kind);
        return link.index >= 0 && link.index < int(table.size()) ? &table[std::size_t(link.index)] : nullptr;
    };
    const auto inheritedNames = [&](EntityKind kind, int index) {
        std::vector<std::string> found;
        for (const HistoryLink &link : history.origins(kind, index)) {
            if (link.kind != kind || link.relation == HistoryRelation::Generated) continue;
            if (const auto *list = operandName(link)) found.insert(found.end(), list->begin(), list->end());
        }
        normalize(found);
        return found;
    };

    for (FaceId f : result.faces()) {
        auto &list = names.faces[std::size_t(f.index)];
        list = inheritedNames(EntityKind::Face, f.index);
        if (!list.empty()) continue;
        if (labels && f.index < int(labels->size()) && !(*labels)[std::size_t(f.index)].empty()) {
            list.push_back(compact(tag, tag + ":" + (*labels)[std::size_t(f.index)]));
            continue;
        }
        std::vector<std::string> from;
        for (const HistoryLink &link : history.origins(EntityKind::Face, f.index)) {
            if (link.relation != HistoryRelation::Generated) continue;
            if (const auto *source = operandName(link))
                if (!source->empty()) from.push_back(source->front());
        }
        list.push_back(compact(tag, from.empty() ? tag + ":F" + std::to_string(f.index) : tag + ":G(" + joined(from, ",") + ")"));
    }
    for (EdgeId e : result.edges()) {
        auto &list = names.edges[std::size_t(e.index)];
        list = inheritedNames(EntityKind::Edge, e.index);
        if (!list.empty()) continue;
        const Edge &edge = result.edge(e);
        std::vector<std::string> faces;
        for (FinId fin : {edge.forward, edge.backward})
            faces.push_back(fin.valid() ? names.primary(EntityKind::Face, result.finFace(fin).index) : std::string());
        std::sort(faces.begin(), faces.end());
        list.push_back(compact(tag, "E(" + faces[0] + "|" + faces[1] + ")"));
    }
    std::vector<std::vector<std::string>> incident(names.vertices.size());
    for (EdgeId e : result.edges())
        for (VertexId v : {result.edgeStart(e), result.edgeEnd(e)})
            if (v.valid() && std::size_t(v.index) < incident.size()) incident[std::size_t(v.index)].push_back(names.primary(EntityKind::Edge, e.index));
    for (VertexId v : result.vertices()) {
        auto &list = names.vertices[std::size_t(v.index)];
        list = inheritedNames(EntityKind::Vertex, v.index);
        if (!list.empty()) continue;
        list.push_back(compact(tag, "V(" + joined(incident[std::size_t(v.index)], "|") + ")"));
    }
    return names;
}

}
