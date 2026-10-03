#include "fk_sew.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>

#include "fk_curve_algo.h"
#include "fk_exchange.h"
#include "fk_intersect.h"
#include "fk_mass.h"

namespace ForgeCad::Kernel {

namespace {

// Le facce come modello grezzo: punti, edge con tratto e loop, come nei file.
// `only` (facoltativo) limita le facce del primo body a quelle indicate.
detail::RawModel rawModel(const std::vector<const Body *> &bodies, std::vector<int> &edgeBody, std::vector<bool> &boundary,
                          const std::vector<FaceId> *only = nullptr) {
    detail::RawModel model;
    for (std::size_t b = 0; b < bodies.size(); ++b) {
        const Body &body = *bodies[b];
        const auto wanted = [&](FaceId f) { return !only || std::find(only->begin(), only->end(), f) != only->end(); };
        std::map<int, int> pointOf, edgeOf;
        for (VertexId v : body.vertices()) {
            pointOf[v.index] = int(model.points.size());
            model.points.push_back(body.vertex(v).point);
        }
        for (EdgeId e : body.edges()) {
            const Edge &edge = body.edge(e);
            if (!edge.curve) continue;
            detail::RawEdge raw;
            raw.start = pointOf.at(body.edgeStart(e).index);
            raw.end = pointOf.at(body.edgeEnd(e).index);
            raw.curve = edge.curve;
            raw.hasRange = true;
            raw.range = edge.range;
            edgeOf[e.index] = int(model.edges.size());
            model.edges.push_back(raw);
            edgeBody.push_back(int(b));
            boundary.push_back(!(edge.forward.valid() && edge.backward.valid()));
        }
        for (FaceId f : body.faces()) {
            if (!wanted(f)) continue;
            const Face &face = body.face(f);
            detail::RawFace raw;
            raw.surface = face.surface;
            raw.sense = face.sense;
            for (LoopId l : face.loops) {
                std::vector<detail::RawFin> loop;
                for (FinId fin : body.loopFins(l)) loop.push_back({edgeOf.at(body.fin(fin).edge.index), body.fin(fin).sense});
                if (!loop.empty()) raw.loops.push_back(std::move(loop));
            }
            model.faces.push_back(std::move(raw));
        }
    }
    return model;
}

// Giunzioni a T: un vertice di un'altra superficie che sta (entro la
// tolleranza) all'interno di un bordo libero lo divide in quel punto.
void splitAtJunctions(detail::RawModel &model, const std::vector<int> &edgeBody, const std::vector<bool> &boundary, double tolerance) {
    // Vertici dei bordi liberi, per superficie.
    std::vector<std::pair<int, int>> boundaryPoints;  // (punto, superficie)
    for (std::size_t e = 0; e < boundary.size(); ++e)
        if (boundary[e])
            for (int p : {model.edges[e].start, model.edges[e].end}) boundaryPoints.push_back({p, edgeBody[e]});
    std::sort(boundaryPoints.begin(), boundaryPoints.end());
    boundaryPoints.erase(std::unique(boundaryPoints.begin(), boundaryPoints.end()), boundaryPoints.end());
    std::map<int, std::vector<int>> replacement;  // edge -> edge nuovi nel verso della curva
    const std::size_t original = boundary.size();
    for (std::size_t e = 0; e < original; ++e) {
        if (!boundary[e]) continue;
        const detail::RawEdge edge = model.edges[e];
        const Box box = curveBox(*edge.curve, edge.range).padded(tolerance);
        const Vec3 &a = model.points[std::size_t(edge.start)], &b = model.points[std::size_t(edge.end)];
        std::vector<std::pair<double, int>> cuts;
        for (const auto &[p, owner] : boundaryPoints) {
            if (owner == edgeBody[e]) continue;
            const Vec3 &q = model.points[std::size_t(p)];
            if (q.x() < box.lo.x() || q.y() < box.lo.y() || q.z() < box.lo.z() || q.x() > box.hi.x() || q.y() > box.hi.y() || q.z() > box.hi.z())
                continue;
            if (distance(q, a) <= tolerance || distance(q, b) <= tolerance) continue;
            const CurveProjection<3> projection = projectPoint(*edge.curve, q, edge.range);
            if (projection.distance > tolerance) continue;
            const double margin = 1e-9 * edge.range.length();
            if (projection.parameter <= edge.range.lo + margin || projection.parameter >= edge.range.hi - margin) continue;
            cuts.push_back({projection.parameter, p});
        }
        if (cuts.empty()) continue;
        std::sort(cuts.begin(), cuts.end());
        std::vector<int> pieces;
        double lo = edge.range.lo;
        int start = edge.start;
        for (const auto &[t, p] : cuts) {
            if (t - lo < 1e-9 * edge.range.length()) continue;
            detail::RawEdge piece = edge;
            piece.start = start;
            piece.end = p;
            piece.range = {lo, t};
            pieces.push_back(int(model.edges.size()));
            model.edges.push_back(piece);
            lo = t;
            start = p;
        }
        detail::RawEdge last = edge;
        last.start = start;
        last.range = {lo, edge.range.hi};
        pieces.push_back(int(model.edges.size()));
        model.edges.push_back(last);
        replacement[int(e)] = std::move(pieces);
    }
    if (replacement.empty()) return;
    for (detail::RawFace &face : model.faces)
        for (auto &loop : face.loops) {
            std::vector<detail::RawFin> fins;
            for (const detail::RawFin &fin : loop) {
                const auto found = replacement.find(fin.edge);
                if (found == replacement.end()) {
                    fins.push_back(fin);
                    continue;
                }
                if (fin.sense)
                    for (int piece : found->second) fins.push_back({piece, true});
                else
                    for (auto it = found->second.rbegin(); it != found->second.rend(); ++it) fins.push_back({*it, false});
            }
            loop = std::move(fins);
        }
}

}  // namespace

Body facesAsSheet(const Body &body, const std::vector<FaceId> &faces) {
    if (faces.empty()) throw std::domain_error("nessuna faccia scelta");
    std::vector<int> edgeBody;
    std::vector<bool> boundary;
    detail::RawModel model = rawModel({&body}, edgeBody, boundary, &faces);
    // Solo i punti e gli edge delle facce scelte (gli altri restano sciolti nel modello e si scartano).
    detail::RawModel used;
    std::map<int, int> pointOf, edgeOf;
    const auto point = [&](int p) {
        auto found = pointOf.find(p);
        if (found != pointOf.end()) return found->second;
        used.points.push_back(model.points[std::size_t(p)]);
        return pointOf[p] = int(used.points.size()) - 1;
    };
    for (detail::RawFace face : model.faces) {
        for (auto &loop : face.loops)
            for (detail::RawFin &fin : loop) {
                auto found = edgeOf.find(fin.edge);
                if (found == edgeOf.end()) {
                    detail::RawEdge edge = model.edges[std::size_t(fin.edge)];
                    edge.start = point(edge.start);
                    edge.end = point(edge.end);
                    used.edges.push_back(edge);
                    found = edgeOf.emplace(fin.edge, int(used.edges.size()) - 1).first;
                }
                fin.edge = found->second;
            }
        used.faces.push_back(std::move(face));
    }
    return detail::assembleBody(used, false);
}

SewResult sewSheets(const std::vector<const Body *> &bodies, double tolerance, bool makeSolid) {
    if (bodies.empty()) throw std::domain_error("cucitura: nessuna superficie");
    std::vector<int> edgeBody;
    std::vector<bool> boundary;
    detail::RawModel model = rawModel(bodies, edgeBody, boundary);
    if (model.faces.empty()) throw std::domain_error("cucitura: nessuna faccia");
    splitAtJunctions(model, edgeBody, boundary, tolerance);
    SewResult result;
    result.closed = detail::sewModel(model, tolerance);
    result.solid = result.closed && makeSolid;
    // Edge usati da una faccia sola (dopo la cucitura).
    std::map<int, int> uses;
    for (const detail::RawFace &face : model.faces)
        for (const auto &loop : face.loops)
            for (const detail::RawFin &fin : loop) ++uses[fin.edge];
    for (const auto &[edge, n] : uses)
        if (n == 1) ++result.freeEdges;
    result.body = detail::assembleBody(model, result.solid, &result.notes);
    if (result.solid && massProperties(result.body).volume < 0.0) {
        // Normali verso l'interno: tutte le facce si girano.
        for (detail::RawFace &face : model.faces) {
            face.sense = !face.sense;
            for (auto &loop : face.loops) {
                std::reverse(loop.begin(), loop.end());
                for (detail::RawFin &fin : loop) fin.sense = !fin.sense;
            }
        }
        result.notes.clear();
        result.body = detail::assembleBody(model, true, &result.notes);
    }
    for (ShellId s : result.body.shells()) {
        (void)s;
        ++result.shells;
    }
    return result;
}

}
