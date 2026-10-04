#include "fk_sew.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>

#include "fk_curve_algo.h"
#include "fk_exchange.h"
#include "fk_intersect.h"
#include "fk_mass.h"
#include "fk_parallel.h"
#include "fk_surface_algo.h"

namespace ForgeCad::Kernel {

namespace {

// Le facce come modello grezzo: punti, edge con tratto e loop, come nei file.
// `only` (facoltativo) limita le facce del primo body a quelle indicate.
detail::RawModel rawModel(const std::vector<const Body *> &bodies, std::vector<int> &edgeBody, std::vector<bool> &boundary,
                          const std::vector<FaceId> *only = nullptr, std::vector<int> *faceBody = nullptr) {
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
            if (faceBody) faceBody->push_back(int(b));
        }
    }
    return model;
}

// Una superficie sostitutiva non conosce necessariamente i trim della faccia
// eliminata: un cilindro nuovo, per esempio, nasce con due soli cerchi anche
// se il collo originale era stato tagliato da una filettatura. Se un body di
// input e' una singola faccia e i bordi liberi degli altri body giacciono sul
// suo supporto, usa quei bordi come loop della faccia prima della cucitura.
// Gli edge sono condivisi direttamente: non rimangono copie coincidenti che
// impediscono alla shell di chiudersi.
void imprintReplacementFaces(detail::RawModel &model, const std::vector<int> &edgeBody,
                             std::vector<bool> &boundary, const std::vector<int> &faceBody,
                             double tolerance) {
    if (faceBody.size() != model.faces.size()) return;
    int bodyCount = 0;
    for (int body : faceBody) bodyCount = std::max(bodyCount, body + 1);
    std::vector<int> facesPerBody(std::size_t(bodyCount), 0);
    for (int body : faceBody) ++facesPerBody[std::size_t(body)];

    const auto usage = [&]() {
        std::vector<std::vector<std::pair<int, bool>>> uses(model.edges.size());
        for (std::size_t f = 0; f < model.faces.size(); ++f)
            for (const auto &loop : model.faces[f].loops)
                for (const detail::RawFin &fin : loop) uses[std::size_t(fin.edge)].push_back({int(f), fin.sense});
        return uses;
    };
    auto uses = usage();
    const double limit = 10.0 * tolerance;
    for (std::size_t faceIndex = 0; faceIndex < model.faces.size(); ++faceIndex) {
        const int owner = faceBody[faceIndex];
        if (facesPerBody[std::size_t(owner)] != 1) continue;
        detail::RawFace &replacement = model.faces[faceIndex];
        if (!replacement.surface || replacement.surface->type() != SurfaceType::Cylinder) continue;
        double vMin = std::numeric_limits<double>::infinity(), vMax = -std::numeric_limits<double>::infinity();
        for (const auto &loop : replacement.loops)
            for (const detail::RawFin &fin : loop) {
                const detail::RawEdge &edge = model.edges[std::size_t(fin.edge)];
                for (double f : {0.0, 0.5, 1.0}) {
                    const double t = edge.range.lo + f * edge.range.length();
                    try {
                        const SurfaceProjection projection = projectPoint(*replacement.surface, edge.curve->point(t));
                        vMin = std::min(vMin, projection.v);
                        vMax = std::max(vMax, projection.v);
                    } catch (const std::exception &) {
                    }
                }
            }
        if (!(vMin <= vMax)) continue;

        struct OrientedEdge { int edge = -1; bool sense = true; int start = -1, end = -1; };
        std::vector<OrientedEdge> selected;
        for (std::size_t e = 0; e < model.edges.size(); ++e) {
            if (e >= boundary.size() || !boundary[e] || edgeBody[e] == owner || uses[e].size() != 1) continue;
            const detail::RawEdge &edge = model.edges[e];
            bool onSupport = true;
            for (double f : {0.0, 0.25, 0.5, 0.75, 1.0}) {
                const double t = edge.range.lo + f * edge.range.length();
                try {
                    const SurfaceProjection projection = projectPoint(*replacement.surface, edge.curve->point(t));
                    if (projection.distance <= limit && projection.v >= vMin - limit && projection.v <= vMax + limit) continue;
                } catch (const std::exception &) {
                }
                onSupport = false;
                break;
            }
            if (!onSupport) continue;
            const bool sense = !uses[e].front().second;
            selected.push_back({int(e), sense, sense ? edge.start : edge.end, sense ? edge.end : edge.start});
        }
        if (selected.empty()) continue;

        std::vector<bool> used(selected.size(), false);
        std::vector<std::vector<detail::RawFin>> loops;
        bool complete = true;
        for (std::size_t seed = 0; seed < selected.size() && complete; ++seed) {
            if (used[seed]) continue;
            std::vector<detail::RawFin> loop;
            const int origin = selected[seed].start;
            int at = origin;
            while (true) {
                int found = -1;
                for (std::size_t k = 0; k < selected.size(); ++k)
                    if (!used[k] && selected[k].start == at) {
                        if (found >= 0) { found = -2; break; }
                        found = int(k);
                    }
                if (found < 0) {
                    complete = at == origin && !loop.empty();
                    break;
                }
                used[std::size_t(found)] = true;
                const OrientedEdge &edge = selected[std::size_t(found)];
                loop.push_back({edge.edge, edge.sense});
                at = edge.end;
                if (at == origin) break;
            }
            if (!loop.empty()) loops.push_back(std::move(loop));
        }
        if (!complete || std::find(used.begin(), used.end(), false) != used.end()) continue;
        replacement.loops = std::move(loops);
        uses = usage();
        boundary.assign(model.edges.size(), false);
        for (std::size_t e = 0; e < uses.size(); ++e) boundary[e] = uses[e].size() == 1;
    }
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
    const std::size_t original = boundary.size();
    struct SplitJob {
        std::size_t edge = 0;
        std::vector<std::pair<double, int>> cuts;
        std::string failure;
    };
    std::vector<SplitJob> jobs;
    for (std::size_t e = 0; e < original; ++e)
        if (boundary[e]) jobs.push_back({e, {}, {}});
    // Le proiezioni di ogni bordo sono indipendenti. Non si modifica il modello
    // dai worker: i nuovi edge vengono materializzati sotto, nello stesso ordine
    // dell'implementazione seriale, dopo che tutti i trim sono stati calcolati.
    parallelFor(jobs.size(), threadCount(0), [&](std::size_t index) {
        SplitJob &job = jobs[index];
        try {
            const std::size_t e = job.edge;
            const detail::RawEdge &edge = model.edges[e];
            const Box box = curveBox(*edge.curve, edge.range).padded(tolerance);
            const Vec3 &a = model.points[std::size_t(edge.start)], &b = model.points[std::size_t(edge.end)];
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
                job.cuts.push_back({projection.parameter, p});
            }
            std::sort(job.cuts.begin(), job.cuts.end());
        } catch (const std::exception &error) {
            job.failure = error.what();
        }
    });
    std::map<int, std::vector<int>> replacement;  // edge -> edge nuovi nel verso della curva
    for (const SplitJob &job : jobs) {
        if (!job.failure.empty()) throw std::domain_error(job.failure);
        if (job.cuts.empty()) continue;
        const std::size_t e = job.edge;
        const detail::RawEdge edge = model.edges[e];
        std::vector<int> pieces;
        double lo = edge.range.lo;
        int start = edge.start;
        for (const auto &[t, p] : job.cuts) {
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
    std::vector<int> faceBody;
    detail::RawModel model = rawModel(bodies, edgeBody, boundary, nullptr, &faceBody);
    if (model.faces.empty()) throw std::domain_error("cucitura: nessuna faccia");
    splitAtJunctions(model, edgeBody, boundary, tolerance);
    SewResult result;
    result.closed = detail::sewModel(model, tolerance);
    if (makeSolid && !result.closed) {
        edgeBody.clear();
        boundary.clear();
        faceBody.clear();
        model = rawModel(bodies, edgeBody, boundary, nullptr, &faceBody);
        imprintReplacementFaces(model, edgeBody, boundary, faceBody, tolerance);
        splitAtJunctions(model, edgeBody, boundary, tolerance);
        result.closed = detail::sewModel(model, tolerance);
    }
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
