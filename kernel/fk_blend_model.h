#ifndef FORGECAD_FK_BLEND_MODEL_H
#define FORGECAD_FK_BLEND_MODEL_H

#include <map>
#include <stdexcept>
#include <vector>

#include "fk_blend.h"
#include "fk_curve_algo.h"
#include "fk_surface_algo.h"
#include "fk_topology.h"

// Interno ai raccordi senza booleane (fk_blend_loop, fk_blend_surface): il
// body come liste modificabili di Body::build, per la chirurgia topologica.
namespace ForgeCad::Kernel::detail {

// Distanze di uno smusso asimmetrico sulle facce della fin forward e della
// fin backward dell'edge: la faccia di riferimento e' quella con la normale
// uscente piu' vicina a sides.referenceNormal nel punto medio.
inline std::pair<double, double> chamferDistances(const Body &body, EdgeId e, const ChamferSides &sides) {
    const Edge &edge = body.edge(e);
    const double t = 0.5 * (edge.range.lo + edge.range.hi);
    const Vec3 p = edge.curve->point(t);
    auto normalOf = [&](FinId fin) {
        const Face &face = body.face(body.finFace(fin));
        Vec2 uv;
        if (body.fin(fin).pcurve) {
            uv = body.fin(fin).pcurve->point(t);
        } else {
            const SurfaceProjection projection = projectPoint(*face.surface, p);
            uv = Vec2(projection.u, projection.v);
        }
        const Vec3 n = normalAt(*face.surface, uv.x(), uv.y());
        return face.sense ? n : -n;
    };
    const double a = dot(normalOf(edge.forward), sides.referenceNormal), b = dot(normalOf(edge.backward), sides.referenceNormal);
    return a >= b ? std::make_pair(sides.onReference, sides.onOther) : std::make_pair(sides.onOther, sides.onReference);
}

// Modello modificabile del body: le liste di Body::build.
struct BlendModel {
    std::vector<Vec3> points;
    std::vector<double> pointTolerance;
    std::vector<Body::BuildEdge> edges;
    std::vector<bool> edgeAlive;
    std::vector<Body::BuildFace> faces;
    std::map<int, int> vertexIndex, edgeIndex, faceIndex;

    bool sheet = false;  // lamina: si ricostruisce con Body::buildSheet

    explicit BlendModel(const Body &body) : sheet(body.isSheet()) {
        for (VertexId v : body.vertices()) {
            vertexIndex[v.index] = int(points.size());
            points.push_back(body.vertex(v).point);
            pointTolerance.push_back(body.vertex(v).tolerance);
        }
        for (EdgeId e : body.edges()) {
            const Edge &edge = body.edge(e);
            edgeIndex[e.index] = int(edges.size());
            edges.push_back({vertexIndex.at(body.edgeStart(e).index), vertexIndex.at(body.edgeEnd(e).index), edge.curve, edge.range, edge.tolerance});
            edgeAlive.push_back(true);
        }
        for (FaceId f : body.faces()) {
            const Face &face = body.face(f);
            faceIndex[f.index] = int(faces.size());
            Body::BuildFace built;
            built.surface = face.surface;
            built.sense = face.sense;
            for (LoopId l : face.loops) {
                std::vector<Body::BuildFin> loop;
                for (FinId fin : body.loopFins(l)) {
                    const Fin &data = body.fin(fin);
                    loop.push_back({edgeIndex.at(data.edge.index), data.sense, data.pcurve, data.pcurveTolerance});
                }
                if (!loop.empty()) built.loops.push_back(std::move(loop));
            }
            faces.push_back(std::move(built));
        }
    }
    int addPoint(const Vec3 &p) {
        points.push_back(p);
        pointTolerance.push_back(0.0);
        return int(points.size()) - 1;
    }
    int addEdge(int start, int end, CurvePtr<3> curve, const Interval &range, double tolerance = 0.0) {
        edges.push_back({start, end, std::move(curve), range, tolerance});
        edgeAlive.push_back(true);
        return int(edges.size()) - 1;
    }
    // Sposta l'estremo `vertex` dell'edge nel punto `point` (nuovo vertice) accorciandone il tratto.
    // Con `extend` un edge rettilineo si puo' anche allungare (un raccordo che
    // finisce in un angolo concavo contro un piano normale al bordo).
    // endWindow ammette anche un prolungamento locale di un arco, gia'
    // delimitato dal chiamante sul solo estremo interessato.
    void moveEnd(int edge, int vertex, int point, bool extend = false, const Interval *endWindow = nullptr) {
        Body::BuildEdge &e = edges[std::size_t(edge)];
        Interval window = e.range;
        if (extend && e.curve->type() == CurveType::Line) {
            const double reach = 2.0 * (e.range.length() + distance(points[std::size_t(point)], e.curve->point(e.range.lo)));
            window = {e.range.lo - reach, e.range.hi + reach};
        }
        if (endWindow) window = *endWindow;
        const double t = projectPoint(*e.curve, points[std::size_t(point)], window).parameter;
        if (extend && (t <= e.range.lo || t >= e.range.hi)) {
            // Allungamento: l'estremo spostato deve restare dalla sua parte.
            if ((e.start == vertex && t < e.range.hi) || (e.end == vertex && t > e.range.lo)) {
                if (e.start == vertex) {
                    e.range.lo = t;
                    e.start = point;
                } else {
                    e.range.hi = t;
                    e.end = point;
                }
                return;
            }
        }
        if (e.start == vertex) {
            if (!(t > e.range.lo && t < e.range.hi))
                throw std::domain_error("blendEdges: raggio troppo grande per lo spigolo vicino " + std::to_string(edge)
                                        + " (parametro " + std::to_string(t) + ", intervallo "
                                        + std::to_string(e.range.lo) + ".." + std::to_string(e.range.hi) + ")");
            e.range.lo = t;
            e.start = point;
        } else if (e.end == vertex) {
            if (!(t > e.range.lo && t < e.range.hi))
                throw std::domain_error("blendEdges: raggio troppo grande per lo spigolo vicino " + std::to_string(edge)
                                        + " (parametro " + std::to_string(t) + ", intervallo "
                                        + std::to_string(e.range.lo) + ".." + std::to_string(e.range.hi) + ")");
            e.range.hi = t;
            e.end = point;
        } else {
            throw std::logic_error("blendEdges: edge senza il vertice");
        }
    }
    // Sostituisce l'edge `from` con `to` (stesso verso) nelle fin della faccia.
    void replaceFin(int face, int from, int to) {
        for (auto &loop : faces[std::size_t(face)].loops)
            for (Body::BuildFin &fin : loop)
                if (fin.edge == from) {
                    fin.edge = to;
                    fin.pcurve = nullptr;
                    fin.pcurveTolerance = 0.0;
                    return;
                }
        throw std::logic_error("blendEdges: fin non trovata");
    }
    // Inserisce nel loop della faccia che passa per `vertex` la fin di `edge`
    // subito dopo la fin che vi arriva.
    void insertFinAfter(int face, int vertex, int edge, bool sense) {
        for (auto &loop : faces[std::size_t(face)].loops)
            for (std::size_t k = 0; k < loop.size(); ++k) {
                const Body::BuildEdge &e = edges[std::size_t(loop[k].edge)];
                const int end = loop[k].sense ? e.end : e.start;
                if (end != vertex) continue;
                loop.insert(loop.begin() + std::ptrdiff_t(k + 1), Body::BuildFin{edge, sense, nullptr, 0.0});
                return;
            }
        throw std::logic_error("blendEdges: vertice non trovato nel loop");
    }
    Body build() const {
        // Solo i vertici usati, rinumerati.
        std::vector<int> used(points.size(), -1);
        std::vector<Vec3> kept;
        std::vector<double> keptTolerance;
        std::vector<Body::BuildEdge> keptEdges;
        std::vector<int> edgeMap(edges.size(), -1);
        auto point = [&](int index) {
            if (used[std::size_t(index)] < 0) {
                used[std::size_t(index)] = int(kept.size());
                kept.push_back(points[std::size_t(index)]);
                keptTolerance.push_back(pointTolerance[std::size_t(index)]);
            }
            return used[std::size_t(index)];
        };
        std::vector<Body::BuildFace> keptFaces = faces;
        for (Body::BuildFace &face : keptFaces)
            for (auto &loop : face.loops)
                for (Body::BuildFin &fin : loop) {
                    if (edgeMap[std::size_t(fin.edge)] < 0) {
                        Body::BuildEdge e = edges[std::size_t(fin.edge)];
                        e.start = point(e.start);
                        e.end = point(e.end);
                        edgeMap[std::size_t(fin.edge)] = int(keptEdges.size());
                        keptEdges.push_back(e);
                    }
                    fin.edge = edgeMap[std::size_t(fin.edge)];
                }
        Body body = sheet ? Body::buildSheet(kept, keptEdges, keptFaces) : Body::build(kept, keptEdges, keptFaces);
        const std::vector<VertexId> vertices = body.vertices();
        for (VertexId v : vertices)
            for (std::size_t k = 0; k < kept.size(); ++k)
                if (keptTolerance[k] > 0.0 && distance(body.vertex(v).point, kept[k]) == 0.0) body.vertex(v).tolerance = keptTolerance[k];
        return body;
    }
};

}

#endif
