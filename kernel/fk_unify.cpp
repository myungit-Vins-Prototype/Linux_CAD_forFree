#include "fk_unify.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <stdexcept>

#include "fk_curve_algo.h"
#include "fk_pcurve.h"

namespace ForgeCad::Kernel {
namespace {

bool sameDomain(const Face &a, const Face &b, double tolerance) {
    if (a.surface == b.surface) return a.sense == b.sense;
    if (a.surface->type() != SurfaceType::Plane || b.surface->type() != SurfaceType::Plane) return false;
    const Frame3 &fa = static_cast<const Plane &>(*a.surface).frame(), &fb = static_cast<const Plane &>(*b.surface).frame();
    const Vec3 na = a.sense ? fa.zDir() : -fa.zDir(), nb = b.sense ? fb.zDir() : -fb.zDir();
    return dot(na, nb) > 1.0 - 1e-12 && std::fabs(dot(na, fb.origin() - fa.origin())) <= tolerance;
}

struct Lists {
    std::vector<Vec3> vertices;
    std::vector<Body::BuildEdge> edges;
    std::vector<bool> alive;
    std::vector<Body::BuildFace> faces;
};

double angleOn(const Circle<3> &circle, const Vec3 &p) {
    const Vec3 d = p - circle.center();
    return std::atan2(dot(d, circle.yAxis()), dot(d, circle.xAxis()));
}

double positiveModulo(double x, double period) {
    x = std::fmod(x, period);
    return x < 0.0 ? x + period : x;
}

// Edge che unisce e1 (con il vertice v in comune) ed e2 sulla stessa curva:
// curva, tratto e vertici, con il parametro crescente da start a end.
// `samePointer`: stessa curva di prima (le SP-curve restano valide).
bool mergeEdges(const Lists &lists, int e1, int e2, int v, double tolerance, Body::BuildEdge &merged, bool &samePointer) {
    const Body::BuildEdge &a = lists.edges[e1], &b = lists.edges[e2];
    const bool forward1 = a.end == v, forward2 = b.start == v;  // e1 va verso v, e2 parte da v
    const int far1 = forward1 ? a.start : a.end, far2 = forward2 ? b.end : b.start;
    merged.tolerance = std::max(a.tolerance, b.tolerance);
    samePointer = false;
    auto near = [](double s, double t) { return std::fabs(s - t) <= 1e-9 * (1.0 + std::fabs(s) + std::fabs(t)); };
    if (a.curve == b.curve) {
        if (forward1 != forward2) return false;
        const Curve<3> &curve = *a.curve;
        // Tratti contigui (sulle curve periodiche a meno di un periodo).
        const Interval first = forward1 ? a.range : b.range, second = forward1 ? b.range : a.range;
        double shift = 0.0;
        if (curve.isPeriodic()) shift = curve.period() * std::round((first.hi - second.lo) / curve.period());
        if (!near(first.hi, second.lo + shift)) return false;
        merged.curve = a.curve;
        merged.range = {first.lo, second.hi + shift};
        if (curve.isPeriodic() && merged.range.length() > curve.period() * (1.0 + 1e-12)) return false;
        merged.start = forward1 ? far1 : far2;
        merged.end = forward1 ? far2 : far1;
        samePointer = true;
        return true;
    }
    const Vec3 pa = lists.vertices[far1], pv = lists.vertices[v], pb = lists.vertices[far2];
    if (a.curve->type() == CurveType::Line && b.curve->type() == CurveType::Line) {
        const auto &la = static_cast<const Line<3> &>(*a.curve), &lb = static_cast<const Line<3> &>(*b.curve);
        if (norm(cross(la.direction(), lb.direction())) > 1e-12 || far1 == far2) return false;
        const Vec3 d = normalized(pb - pa);
        if (norm(cross(pv - pa, d)) > tolerance || dot(pv - pa, d) <= 0.0 || dot(pb - pv, d) <= 0.0) return false;
        merged.curve = std::make_shared<Line<3>>(pa, d);
        merged.range = {0.0, distance(pa, pb)};
        merged.start = far1;
        merged.end = far2;
        return true;
    }
    if (a.curve->type() == CurveType::Circle && b.curve->type() == CurveType::Circle) {
        const auto &ca = static_cast<const Circle<3> &>(*a.curve), &cb = static_cast<const Circle<3> &>(*b.curve);
        const Vec3 normalA = cross(ca.xAxis(), ca.yAxis()), normalB = cross(cb.xAxis(), cb.yAxis());
        if (distance(ca.center(), cb.center()) > tolerance || std::fabs(ca.radius() - cb.radius()) > tolerance
            || std::fabs(dot(normalA, normalB)) < 1.0 - 1e-12)
            return false;
        const double middle = angleOn(ca, b.curve->point(0.5 * (b.range.lo + b.range.hi)));
        if (forward1) {
            // Parametro crescente da far1 a v lungo e1: si prosegue oltre v.
            const double tv = a.range.hi;
            double tb = far2 == far1 ? a.range.lo + kTwoPi : tv + positiveModulo(angleOn(ca, pb) - tv, kTwoPi);
            if (far2 != far1 && tb - tv < 1e-12) tb += kTwoPi;
            if (!(tv + positiveModulo(middle - tv, kTwoPi) < tb)) return false;
            merged.range = {a.range.lo, tb};
            merged.start = far1;
            merged.end = far2;
        } else {
            const double tv = a.range.lo;
            double tb = far2 == far1 ? a.range.hi - kTwoPi : tv - positiveModulo(tv - angleOn(ca, pb), kTwoPi);
            if (far2 != far1 && tv - tb < 1e-12) tb -= kTwoPi;
            if (!(tv - positiveModulo(tv - middle, kTwoPi) > tb)) return false;
            merged.range = {tb, a.range.hi};
            merged.start = far2;
            merged.end = far1;
        }
        if (merged.range.length() > kTwoPi * (1.0 + 1e-12)) return false;
        merged.curve = a.curve;
        return true;
    }
    return false;
}

// Unisce gli edge nei vertici che separano solo due edge sulla stessa curva.
void mergeCollinearEdges(Lists &lists, double tolerance) {
    for (bool changed = true; changed;) {
        changed = false;
        std::map<int, std::vector<int>> incident;
        for (std::size_t e = 0; e < lists.edges.size(); ++e) {
            if (!lists.alive[e]) continue;
            incident[lists.edges[e].start].push_back(int(e));
            if (lists.edges[e].end != lists.edges[e].start) incident[lists.edges[e].end].push_back(int(e));
            else incident[lists.edges[e].start].push_back(int(e));  // edge chiuso: il vertice resta
        }
        for (const auto &[v, edges] : incident) {
            if (edges.size() != 2 || edges[0] == edges[1]) continue;
            const int e1 = edges[0], e2 = edges[1];
            Body::BuildEdge merged;
            bool samePointer;
            if (!mergeEdges(lists, e1, e2, v, tolerance, merged, samePointer)) continue;
            // Nei loop le due fin sono consecutive: diventano una.
            std::vector<std::pair<std::size_t, std::size_t>> places;  // faccia, loop
            bool consistent = true;
            for (std::size_t f = 0; f < lists.faces.size() && consistent; ++f)
                for (std::size_t l = 0; l < lists.faces[f].loops.size(); ++l) {
                    std::vector<Body::BuildFin> &loop = lists.faces[f].loops[l];
                    for (std::size_t i = 0; i < loop.size(); ++i) {
                        const std::size_t j = (i + 1) % loop.size();
                        const bool pair = (loop[i].edge == e1 && loop[j].edge == e2) || (loop[i].edge == e2 && loop[j].edge == e1);
                        if (!pair || i == j) continue;
                        const Body::BuildFin &first = loop[i];
                        const Body::BuildEdge &firstEdge = lists.edges[first.edge];
                        const double tm = 0.5 * (firstEdge.range.lo + firstEdge.range.hi);
                        const Vec3 travel = firstEdge.curve->derivative(tm) * (first.sense ? 1.0 : -1.0);
                        const CurveProjection<3> onMerged = projectPoint(*merged.curve, firstEdge.curve->point(tm), merged.range);
                        Body::BuildFin fin;
                        fin.edge = e1;
                        fin.sense = dot(travel, merged.curve->derivative(onMerged.parameter)) > 0.0;
                        if (samePointer && loop[i].pcurve && loop[i].pcurve == loop[j].pcurve) {
                            fin.pcurve = loop[i].pcurve;
                            fin.pcurveTolerance = std::max(loop[i].pcurveTolerance, loop[j].pcurveTolerance);
                        }
                        loop[i] = fin;
                        loop.erase(loop.begin() + j);
                        places.emplace_back(f, l);
                        break;
                    }
                }
            if (places.size() != 2) throw std::logic_error("unifySameDomain: fin non consecutive in un vertice di grado 2");
            merged.tolerance = std::max(merged.tolerance, 0.0);
            lists.edges[e1] = merged;
            lists.alive[e2] = false;
            changed = true;
            break;  // l'incidenza e' cambiata
        }
    }
}

}

Body unifySameDomain(const Body &body, double tolerance) {
    const std::vector<FaceId> faceIds = body.faces();
    std::map<int, int> position;
    for (std::size_t i = 0; i < faceIds.size(); ++i) position[faceIds[i].index] = int(i);
    std::vector<int> parent(faceIds.size());
    for (std::size_t i = 0; i < parent.size(); ++i) parent[i] = int(i);
    std::function<int(int)> find = [&](int i) { return parent[i] == i ? i : parent[i] = find(parent[i]); };

    // 1. Facce sulla stessa superficie unite attraverso gli edge.
    std::map<int, bool> internal;  // edge -> tra due facce dello stesso gruppo
    for (EdgeId e : body.edges()) {
        const Edge &edge = body.edge(e);
        const FaceId f1 = body.finFace(edge.forward), f2 = body.finFace(edge.backward);
        if (f1 == f2 || sameDomain(body.face(f1), body.face(f2), tolerance)) parent[find(position[f1.index])] = find(position[f2.index]);
    }
    for (EdgeId e : body.edges()) {
        const Edge &edge = body.edge(e);
        internal[e.index] = find(position[body.finFace(edge.forward).index]) == find(position[body.finFace(edge.backward).index]);
    }

    // 2. Loop dei gruppi: dopo una fin che resta viene la prossima che resta
    // girando attorno al vertice (next della fin gemella degli edge tolti).
    Lists lists;
    std::map<int, int> vertexIndex, edgeIndex;
    auto vertexOf = [&](VertexId v) {
        const auto found = vertexIndex.find(v.index);
        if (found != vertexIndex.end()) return found->second;
        lists.vertices.push_back(body.vertex(v).point);
        return vertexIndex[v.index] = int(lists.vertices.size()) - 1;
    };
    auto edgeOf = [&](EdgeId e) {
        const auto found = edgeIndex.find(e.index);
        if (found != edgeIndex.end()) return found->second;
        const Edge &edge = body.edge(e);
        Body::BuildEdge spec;
        spec.curve = edge.curve;
        spec.range = edge.range;
        spec.tolerance = edge.tolerance;
        spec.start = vertexOf(body.edgeStart(e));
        spec.end = vertexOf(body.edgeEnd(e));
        lists.edges.push_back(spec);
        lists.alive.push_back(true);
        return edgeIndex[e.index] = int(lists.edges.size()) - 1;
    };
    std::map<int, std::vector<FaceId>> groups;
    for (std::size_t i = 0; i < faceIds.size(); ++i) groups[find(int(i))].push_back(faceIds[i]);
    for (const auto &[root, members] : groups) {
        const Face &representative = body.face(members.front());
        Body::BuildFace built;
        built.surface = representative.surface;
        built.sense = representative.sense;
        std::map<int, bool> visited;
        for (FaceId f : members)
            for (LoopId l : body.face(f).loops)
                for (FinId start : body.loopFins(l)) {
                    if (internal[body.fin(start).edge.index] || visited[start.index]) continue;
                    std::vector<Body::BuildFin> loop;
                    FinId fin = start;
                    for (std::size_t guard = 0;; ++guard) {
                        if (guard > 100000) throw std::logic_error("unifySameDomain: loop non chiuso");
                        visited[fin.index] = true;
                        const Fin &data = body.fin(fin);
                        Body::BuildFin spec;
                        spec.edge = edgeOf(data.edge);
                        spec.sense = data.sense;
                        if (body.face(body.finFace(fin)).surface == representative.surface) {
                            spec.pcurve = data.pcurve;
                            spec.pcurveTolerance = data.pcurveTolerance;
                        }
                        loop.push_back(spec);
                        FinId next = data.next;
                        for (std::size_t turn = 0; internal[body.fin(next).edge.index]; ++turn) {
                            if (turn > 100000) throw std::logic_error("unifySameDomain: vertice senza uscita");
                            next = body.fin(body.otherFin(next)).next;
                        }
                        fin = next;
                        if (fin == start) break;
                    }
                    built.loops.push_back(std::move(loop));
                }
        if (!built.loops.empty()) lists.faces.push_back(std::move(built));
    }

    // 3. Edge sulla stessa curva separati da un vertice che non serve piu'.
    mergeCollinearEdges(lists, tolerance);

    // Solo gli edge e i vertici rimasti.
    std::vector<int> newEdge(lists.edges.size(), -1), newVertex(lists.vertices.size(), -1);
    std::vector<Body::BuildEdge> edges;
    std::vector<Vec3> vertices;
    for (std::size_t e = 0; e < lists.edges.size(); ++e) {
        if (!lists.alive[e]) continue;
        Body::BuildEdge spec = lists.edges[e];
        for (int *v : {&spec.start, &spec.end}) {
            if (newVertex[*v] < 0) {
                newVertex[*v] = int(vertices.size());
                vertices.push_back(lists.vertices[*v]);
            }
            *v = newVertex[*v];
        }
        newEdge[e] = int(edges.size());
        edges.push_back(spec);
    }
    for (Body::BuildFace &face : lists.faces)
        for (std::vector<Body::BuildFin> &loop : face.loops)
            for (Body::BuildFin &fin : loop) fin.edge = newEdge[fin.edge];
    Body result = Body::build(vertices, edges, lists.faces);
    computePCurves(result);
    return result;
}

}
