#include "fk_blend_loop.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>
#include <string>

#include "fk_body_check.h"
#include "fk_bspline.h"
#include "fk_bspline_surface.h"
#include "fk_classify.h"
#include "fk_curve_algo.h"
#include "fk_intersect.h"
#include "fk_marching.h"
#include "fk_pcurve.h"
#include "fk_precision.h"
#include "fk_surface_algo.h"

namespace ForgeCad::Kernel {
namespace {

constexpr double kFitTolerance = 1e-9;  // scarto delle curve di controllo dalle curve vere
constexpr double kParallel = 1e-9;

// Normale del piano (uscente dalla faccia), se la faccia e' piana.
bool planeNormal(const Body &body, FaceId f, Vec3 &normal) {
    const Face &face = body.face(f);
    if (face.surface->type() != SurfaceType::Plane) return false;
    const Vec3 z = static_cast<const Plane &>(*face.surface).frame().zDir();
    normal = face.sense ? z : -z;
    return true;
}

// La superficie contiene le rette parallele a `direction` (fianco di un'estrusione lungo di essa).
bool parallelWall(const Surface &surface, const Vec3 &direction) {
    auto parallel = [&](const Vec3 &a) { return norm(cross(normalized(a), direction)) <= kParallel; };
    switch (surface.type()) {
    case SurfaceType::Plane: return std::fabs(dot(static_cast<const Plane &>(surface).frame().zDir(), direction)) <= kParallel;
    case SurfaceType::Cylinder: return parallel(static_cast<const CylindricalSurface &>(surface).frame().zDir());
    case SurfaceType::Extrusion: return parallel(static_cast<const ExtrusionSurface &>(surface).direction());
    default: return false;
    }
}

// Facce piane candidate a essere T per l'edge (l'altra faccia e' un fianco parallelo alla normale).
std::vector<FaceId> planarSides(const Body &body, EdgeId e) {
    std::vector<FaceId> result;
    const Edge &edge = body.edge(e);
    if (!edge.curve || body.isLaminar(e)) return result;
    const FaceId faces[2] = {body.finFace(edge.forward), body.finFace(edge.backward)};
    if (faces[0] == faces[1]) return result;
    for (int k = 0; k < 2; ++k) {
        Vec3 normal;
        if (planeNormal(body, faces[k], normal) && parallelWall(*body.face(faces[1 - k]).surface, normal)) result.push_back(faces[k]);
    }
    return result;
}

// Modello modificabile del body: le liste di Body::build.
struct Model {
    std::vector<Vec3> points;
    std::vector<double> pointTolerance;
    std::vector<Body::BuildEdge> edges;
    std::vector<bool> edgeAlive;
    std::vector<Body::BuildFace> faces;
    std::map<int, int> vertexIndex, edgeIndex, faceIndex;

    explicit Model(const Body &body) {
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
    void moveEnd(int edge, int vertex, int point) {
        Body::BuildEdge &e = edges[std::size_t(edge)];
        const double t = projectPoint(*e.curve, points[std::size_t(point)], e.range).parameter;
        if (e.start == vertex) {
            if (!(t > e.range.lo && t < e.range.hi)) throw std::domain_error("blendEdges: raggio troppo grande per uno spigolo vicino");
            e.range.lo = t;
            e.start = point;
        } else if (e.end == vertex) {
            if (!(t > e.range.lo && t < e.range.hi)) throw std::domain_error("blendEdges: raggio troppo grande per uno spigolo vicino");
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
        Body body = Body::build(kept, keptEdges, keptFaces);
        const std::vector<VertexId> vertices = body.vertices();
        for (VertexId v : vertices)
            for (std::size_t k = 0; k < kept.size(); ++k)
                if (keptTolerance[k] > 0.0 && distance(body.vertex(v).point, kept[k]) == 0.0) body.vertex(v).tolerance = keptTolerance[k];
        return body;
    }
};

// Curve di controllo del raccordo lungo il bordo: righe della superficie,
// cubiche di Hermite a tratti con gli stessi nodi.
struct RowFit {
    std::vector<double> breaks;           // parametri dei nodi (crescenti)
    std::vector<std::vector<Vec3>> poles;  // per riga: 3 N + 1 poli
    double error = 0.0;
};

// Valori e derivate delle righe nel parametro t dell'edge.
struct RowSample {
    Vec3 value[3], derivative[3];
};

class ChainEdge {
public:
    ChainEdge(const Edge &edge, const Vec3 &normal, double sideT, const Vec3 &wall, double size, bool chamfer)
        : curve_(edge.curve), range_(edge.range), normal_(normal), sideT_(sideT), wall_(wall), size_(size), chamfer_(chamfer) {}

    // Righe: 0 traslata lungo il fianco, 1 il bordo (solo raccordo), 2 parallela in T.
    RowSample sample(double t, bool left) const {
        Vec3 d[3];
        if (left) curve_->evaluateLeft(t, 2, d);
        else curve_->evaluate(t, 2, d);
        const Vec3 w = cross(normal_, d[1]), w1 = cross(normal_, d[2]);
        const double length = norm(w);
        if (!(length > 0.0)) throw std::domain_error("blendEdges: bordo con tangente nulla");
        const Vec3 unit = w / length;
        const Vec3 tT = sideT_ * unit, tT1 = sideT_ * (w1 - dot(unit, w1) * unit) / length;
        RowSample s;
        s.value[0] = d[0] + size_ * wall_;
        s.derivative[0] = d[1];
        s.value[1] = d[0];
        s.derivative[1] = d[1];
        s.value[2] = d[0] + size_ * tT;
        s.derivative[2] = d[1] + size_ * tT1;
        return s;
    }
    Vec3 inPlane(double t) const {
        const Vec3 w = cross(normal_, curve_->derivative(t));
        return sideT_ * normalized(w);
    }

    RowFit fit() const { return fit(range_); }
    RowFit fit(const Interval &span) const {
        std::vector<double> breaks = curve_->breakpoints(span);
        // Almeno 8 tratti, poi si dividono quelli che si scostano troppo.
        std::vector<double> start;
        for (std::size_t k = 0; k + 1 < breaks.size(); ++k) {
            const double a = breaks[k], b = breaks[k + 1];
            const int pieces = std::max(1, int(std::ceil(8.0 * (b - a) / span.length())));
            for (int j = 0; j < pieces; ++j) start.push_back(a + (b - a) * j / pieces);
        }
        start.push_back(span.hi);
        RowFit result;
        result.breaks.push_back(start.front());
        const int rows = 3;
        result.poles.assign(rows, {});
        RowSample first = sample(start.front(), false);
        for (int r = 0; r < rows; ++r) result.poles[std::size_t(r)].push_back(first.value[r]);
        // Tratti da elaborare in ordine (pila con il primo in cima).
        std::vector<std::pair<double, double>> pending;
        for (std::size_t k = start.size() - 1; k > 0; --k) pending.push_back({start[k - 1], start[k]});
        int guard = 0;
        while (!pending.empty()) {
            if (++guard > 20000) throw std::domain_error("blendEdges: bordo troppo complesso da raccordare");
            const auto [a, b] = pending.back();
            pending.pop_back();
            const RowSample sa = sample(a, false), sb = sample(b, true);
            const double h = b - a;
            double error = 0.0;
            for (double f : {0.2, 0.5, 0.8}) {
                const double t = a + f * h;
                const RowSample exact = sample(t, false);
                const double h00 = (1 + 2 * f) * (1 - f) * (1 - f), h10 = f * (1 - f) * (1 - f), h01 = f * f * (3 - 2 * f), h11 = f * f * (f - 1);
                for (int r = 0; r < rows; ++r) {
                    const Vec3 p = h00 * sa.value[r] + h10 * h * sa.derivative[r] + h01 * sb.value[r] + h11 * h * sb.derivative[r];
                    error = std::max(error, distance(p, exact.value[r]));
                }
                // La parallela non deve tornare indietro (cuspide: raggio maggiore della curvatura).
                if (!(dot(exact.derivative[2], exact.derivative[1]) > 0.0))
                    throw std::domain_error("blendEdges: raggio maggiore del raggio di curvatura del bordo");
            }
            if (error > kFitTolerance && h > 1e-9 * span.length()) {
                const double m = 0.5 * (a + b);
                pending.push_back({m, b});
                pending.push_back({a, m});
                continue;
            }
            result.error = std::max(result.error, error);
            for (int r = 0; r < rows; ++r) {
                std::vector<Vec3> &p = result.poles[std::size_t(r)];
                p.push_back(sa.value[r] + (h / 3.0) * sa.derivative[r]);
                p.push_back(sb.value[r] - (h / 3.0) * sb.derivative[r]);
                p.push_back(sb.value[r]);
            }
            result.breaks.push_back(b);
        }
        return result;
    }

    // Superficie del raccordo (u = parametro dell'edge, v in [0, 1] dalla traslata alla parallela).
    std::shared_ptr<BSplineSurface> surface(const RowFit &fit) const {
        std::vector<double> uKnots;
        for (std::size_t k = 0; k < fit.breaks.size(); ++k) {
            const int multiplicity = (k == 0 || k + 1 == fit.breaks.size()) ? 4 : 3;
            for (int j = 0; j < multiplicity; ++j) uKnots.push_back(fit.breaks[k]);
        }
        const int uCount = int(fit.poles[0].size());
        std::vector<int> rows = chamfer_ ? std::vector<int>{0, 2} : std::vector<int>{0, 1, 2};
        const int vCount = int(rows.size());
        std::vector<Vec3> poles(std::size_t(uCount * vCount));
        std::vector<double> weights;
        for (int i = 0; i < uCount; ++i)
            for (int j = 0; j < vCount; ++j) poles[std::size_t(i * vCount + j)] = fit.poles[std::size_t(rows[std::size_t(j)])][std::size_t(i)];
        if (!chamfer_) {
            weights.resize(poles.size());
            for (int i = 0; i < uCount; ++i)
                for (int j = 0; j < vCount; ++j) weights[std::size_t(i * vCount + j)] = j == 1 ? std::sqrt(0.5) : 1.0;
        }
        std::vector<double> vKnots = chamfer_ ? std::vector<double>{0, 0, 1, 1} : std::vector<double>{0, 0, 0, 1, 1, 1};
        return std::make_shared<BSplineSurface>(3, chamfer_ ? 1 : 2, uKnots, vKnots, uCount, vCount, poles, weights);
    }

private:
    CurvePtr<3> curve_;
    Interval range_;
    Vec3 normal_;
    double sideT_;
    Vec3 wall_;
    double size_;
    bool chamfer_;
};

struct ChainFin {
    FinId fin;
    EdgeId edge;
    bool sense = true;
    FaceId wall;
    double startParameter = 0.0, endParameter = 0.0;  // parametro dell'edge all'inizio e alla fine (verso del loop)
    std::shared_ptr<ChainEdge> geometry;
    std::shared_ptr<BSplineSurface> surface;
    int face = -1;               // faccia nuova nel modello
    int inPlaneEdge = -1, wallEdge = -1;  // edge nuovi: parallela in T e traslata sul fianco
    double fitError = 0.0;
};

// Tipo di vertice della catena.
enum class Joint { Smooth, Sharp, End };

struct ChainVertex {
    VertexId vertex;
    Joint joint = Joint::Smooth;
    int inPlanePoint = -1, wallPoint = -1;  // nuovi vertici: sulla parallela in T e sul fianco
    int connector = -1;                     // edge tra i due (arco della sezione o curva d'intersezione)
    bool connectorFromWall = true;          // l'edge va dal vertice sul fianco a quello in T
    int vertical = -1;                      // spigolo tra due fianchi (modello), -1 se non c'e'
    int endEdge = -1, endFace = -1;         // estremo di una catena aperta: edge in T e faccia normale
};

std::vector<int> edgesAt(const Model &model, int vertex) {
    std::vector<int> result;
    for (std::size_t e = 0; e < model.edges.size(); ++e)
        if (model.edgeAlive[e] && (model.edges[e].start == vertex || model.edges[e].end == vertex)) result.push_back(int(e));
    return result;
}

bool straightAlong(const Model &model, int e, const Vec3 &direction) {
    const Body::BuildEdge &edge = model.edges[std::size_t(e)];
    const Vec3 a = edge.curve->point(edge.range.lo), b = edge.curve->point(edge.range.hi), m = edge.curve->point(0.5 * (edge.range.lo + edge.range.hi));
    const double length = distance(a, b);
    return length > kLinearResolution && norm(cross((b - a) / length, direction)) <= 1e-9 && norm(cross(m - a, (b - a) / length)) <= 1e-9 * length;
}

}

bool isPlanarChainEdge(const Body &body, EdgeId edge) { return !planarSides(body, edge).empty(); }

std::vector<EdgeId> planarChainRuns(const Body &body, const std::vector<EdgeId> &selected, const std::vector<EdgeId> &seeds) {
    std::set<int> chosen, result;
    for (EdgeId e : selected) chosen.insert(e.index);
    for (EdgeId seed : seeds) {
        const std::vector<FaceId> sides = planarSides(body, seed);
        if (sides.size() != 1)
            throw std::domain_error("blendEdges: bordo di forma libera non tra una faccia piana e un fianco normale ad essa");
        const Edge &edge = body.edge(seed);
        const FinId fin = body.finFace(edge.forward) == sides.front() ? edge.forward : edge.backward;
        const std::vector<FinId> loop = body.loopFins(body.fin(fin).loop);
        const int n = int(loop.size()), at = int(std::find(loop.begin(), loop.end(), fin) - loop.begin());
        auto edgeAt = [&](int k) { return body.fin(loop[std::size_t(((k % n) + n) % n)]).edge.index; };
        result.insert(seed.index);
        for (int k = at + 1; k < at + n && chosen.count(edgeAt(k)); ++k) result.insert(edgeAt(k));
        for (int k = at - 1; k > at - n && chosen.count(edgeAt(k)); --k) result.insert(edgeAt(k));
    }
    std::vector<EdgeId> edges;
    for (int index : result) edges.push_back(EdgeId(index));
    return edges;
}

Body blendPlanarChains(const Body &input, const std::vector<EdgeId> &selected, double size, bool chamfer) {
    if (!(size > kLinearResolution)) throw std::domain_error("blendEdges: raggio o distanza non validi");
    Body body = input;
    std::vector<EdgeId> edges = selected;
    // Un loop fatto di un solo edge chiuso si divide in due (i raccordi sono pezze aperte).
    for (std::size_t k = 0; k < edges.size(); ++k) {
        const EdgeId e = edges[k];
        if (body.edgeStart(e) != body.edgeEnd(e)) continue;
        const Edge &edge = body.edge(e);
        const double middle = 0.5 * (edge.range.lo + edge.range.hi);
        edges.push_back(body.semv(e, edge.curve->point(middle), middle).edge);
    }
    Box box;
    for (VertexId v : body.vertices()) box.add(body.vertex(v).point);
    for (FaceId f : body.faces()) box.add(faceBox(body, f));
    const double scale = std::max(box.diagonal(), 1.0), tolerance = 1e-9 * scale, probe = 1e-4 * std::min(scale, size);

    // Catene: tratti consecutivi di edge scelti in un loop di una faccia piana.
    std::set<int> pending;
    for (EdgeId e : edges) pending.insert(e.index);
    struct Chain {
        FaceId plane;
        Vec3 normal;
        std::vector<ChainFin> fins;
        bool closed = false;
    };
    std::vector<Chain> chains;
    while (!pending.empty()) {
        // Prima gli edge con una sola faccia piana possibile (spline, archi): fissano la faccia della catena.
        int seed = -1;
        FaceId plane;
        for (int index : pending) {
            const std::vector<FaceId> sides = planarSides(body, EdgeId(index));
            if (sides.empty()) throw std::domain_error("blendEdges: spigolo non tra una faccia piana e un fianco normale ad essa");
            if (seed < 0 || sides.size() == 1) {
                seed = index;
                plane = sides.front();
                if (sides.size() == 1) break;
            }
        }
        const Edge &seedEdge = body.edge(EdgeId(seed));
        const FinId seedFin = body.finFace(seedEdge.forward) == plane ? seedEdge.forward : seedEdge.backward;
        const std::vector<FinId> loop = body.loopFins(body.fin(seedFin).loop);
        const int n = int(loop.size());
        const int at = int(std::find(loop.begin(), loop.end(), seedFin) - loop.begin());
        auto chosen = [&](int k) { return pending.count(body.fin(loop[std::size_t(((k % n) + n) % n)]).edge.index) > 0; };
        Chain chain;
        chain.plane = plane;
        planeNormal(body, plane, chain.normal);
        int first = at, count = 1;
        while (count < n && chosen(first - 1)) --first, ++count;
        while (count < n && chosen(first + count)) ++count;
        chain.closed = count == n;
        for (int k = 0; k < count; ++k) {
            ChainFin fin;
            fin.fin = loop[std::size_t(((first + k) % n + n) % n)];
            fin.edge = body.fin(fin.fin).edge;
            fin.sense = body.fin(fin.fin).sense;
            fin.wall = body.finFace(body.otherFin(fin.fin));
            if (!parallelWall(*body.face(fin.wall).surface, chain.normal))
                throw std::domain_error("blendEdges: fianco non normale alla faccia piana");
            const Edge &edge = body.edge(fin.edge);
            fin.startParameter = fin.sense ? edge.range.lo : edge.range.hi;
            fin.endParameter = fin.sense ? edge.range.hi : edge.range.lo;
            pending.erase(fin.edge.index);
            chain.fins.push_back(fin);
        }
        chains.push_back(std::move(chain));
    }

    Model model(body);
    std::set<int> usedVertices;
    for (Chain &chain : chains) {
        const Vec3 D = chain.normal;
        const int n = int(chain.fins.size());
        // Verso della parallela in T e del fianco (dall'edge verso la faccia), convessita'.
        int convexity = 0;
        for (ChainFin &fin : chain.fins) {
            const Edge &edge = body.edge(fin.edge);
            const double middle = 0.5 * (edge.range.lo + edge.range.hi);
            const Vec3 p = edge.curve->point(middle), across = normalized(cross(D, edge.curve->derivative(middle)));
            double sideT = 0.0;
            for (double sign : {1.0, -1.0})
                if (classifyPointOnFace(body, chain.plane, p + sign * probe * across, tolerance) == PointLocation::Inside) {
                    sideT = sign;
                    break;
                }
            Vec3 wall;
            bool foundWall = false;
            for (double sign : {-1.0, 1.0})
                if (classifyPointOnFace(body, fin.wall, p + sign * probe * D, tolerance) == PointLocation::Inside) {
                    wall = sign * D;
                    foundWall = true;
                    break;
                }
            if (sideT == 0.0 || !foundWall) throw std::domain_error("blendEdges: faccia dello spigolo troppo stretta");
            const int convex = dot(wall, D) < 0.0 ? 1 : -1;
            if (convexity != 0 && convex != convexity) throw std::domain_error("blendEdges: catena con spigoli convessi e concavi");
            convexity = convex;
            fin.geometry = std::make_shared<ChainEdge>(edge, D, sideT, wall, size, chamfer);
            const RowFit fit = fin.geometry->fit();
            fin.fitError = fit.error;
            fin.surface = fin.geometry->surface(fit);
            // I punti della parallela devono stare in T, quelli della traslata sul fianco.
            for (double f : {0.25, 0.5, 0.75}) {
                const double t = edge.range.lo + f * edge.range.length();
                const RowSample s = fin.geometry->sample(t, false);
                if (classifyPointOnFace(body, chain.plane, s.value[2], tolerance) != PointLocation::Inside
                    || classifyPointOnFace(body, fin.wall, s.value[0], tolerance) != PointLocation::Inside)
                    throw std::domain_error("blendEdges: raggio troppo grande per le facce dello spigolo");
            }
        }
        const bool convex = convexity > 0;

        // Vertici della catena: n (chiusa) o n + 1 (aperta).
        const int vertexCount = chain.closed ? n : n + 1;
        std::vector<ChainVertex> vertices(static_cast<std::size_t>(vertexCount));
        for (int j = 0; j < vertexCount; ++j) {
            ChainVertex &cv = vertices[std::size_t(j)];
            cv.vertex = j < n ? body.finStart(chain.fins[std::size_t(j)].fin) : body.finEnd(chain.fins[std::size_t(n - 1)].fin);
            if (!usedVertices.insert(cv.vertex.index).second) throw std::domain_error("blendEdges: catene di raccordi che si toccano");
            const int vertex = model.vertexIndex.at(cv.vertex.index);
            const bool hasPrevious = chain.closed || j > 0, hasNext = chain.closed || j < n;
            const ChainFin *previous = hasPrevious ? &chain.fins[std::size_t((j - 1 + n) % n)] : nullptr;
            const ChainFin *next = hasNext ? &chain.fins[std::size_t(j % n)] : nullptr;
            // Punti nuovi, dalla geometria esatta di un edge adiacente.
            const ChainFin &adjacent = next ? *next : *previous;
            const double atVertex = next ? adjacent.startParameter : adjacent.endParameter;
            const RowSample s = adjacent.geometry->sample(atVertex, atVertex == body.edge(adjacent.edge).range.hi);
            cv.wallPoint = model.addPoint(s.value[0]);
            cv.inPlanePoint = model.addPoint(s.value[2]);
            auto loopTangent = [&](const ChainFin &fin, bool atEnd) {
                const Edge &edge = body.edge(fin.edge);
                const double t = atEnd ? fin.endParameter : fin.startParameter;
                const Vec3 d = normalized(edge.curve->derivative(t));
                return fin.sense ? d : -d;
            };
            std::vector<int> others;
            for (int e : edgesAt(model, vertex)) {
                const bool inChain = (previous && e == model.edgeIndex.at(previous->edge.index)) || (next && e == model.edgeIndex.at(next->edge.index));
                if (!inChain) others.push_back(e);
            }
            if (previous && next) {
                const double turn = dot(loopTangent(*previous, true), loopTangent(*next, false));
                cv.joint = turn >= 1.0 - kParallel ? Joint::Smooth : Joint::Sharp;
                if (others.size() > 1 || (others.size() == 1 && !straightAlong(model, others.front(), D)))
                    throw std::domain_error("blendEdges: vertice della catena con altri spigoli");
                if (others.size() == 1) cv.vertical = others.front();
                if (cv.joint == Joint::Sharp) {
                    if (cv.vertical < 0) throw std::domain_error("blendEdges: angolo vivo senza spigolo tra i fianchi");
                    if (dot(cross(loopTangent(*previous, true), loopTangent(*next, false)), D) <= 0.0)
                        throw std::domain_error("blendEdges: angoli vivi concavi della catena non gestiti");
                }
            } else {
                // Estremo di una catena aperta: l'edge di T che segue e la faccia normale al bordo.
                cv.joint = Joint::End;
                const Vec3 tangent = next ? loopTangent(*next, false) : loopTangent(*previous, true);
                const FinId chainFin = next ? next->fin : previous->fin;
                const FinId neighbour = next ? body.fin(chainFin).previous : body.fin(chainFin).next;
                const EdgeId g = body.fin(neighbour).edge;
                const FaceId end = body.finFace(body.otherFin(neighbour));
                Vec3 endNormal;
                if (!planeNormal(body, end, endNormal) || std::fabs(dot(endNormal, tangent)) < 1.0 - kParallel)
                    throw std::domain_error("blendEdges: la catena finisce contro una faccia non normale al bordo (scegli tutto il contorno)");
                cv.endEdge = model.edgeIndex.at(g.index);
                cv.endFace = model.faceIndex.at(end.index);
                if (!straightAlong(model, cv.endEdge, normalized(s.value[2] - s.value[1]))) throw std::domain_error("blendEdges: estremo della catena non gestito");
                for (int e : others)
                    if (e != cv.endEdge) {
                        if (cv.vertical >= 0 || !straightAlong(model, e, D)) throw std::domain_error("blendEdges: estremo della catena con altri spigoli");
                        cv.vertical = e;
                    }
                if (cv.vertical < 0) throw std::domain_error("blendEdges: estremo della catena senza spigolo sul fianco");
            }
        }

        // Facce e edge nuovi dei raccordi.
        for (int i = 0; i < n; ++i) {
            ChainFin &fin = chain.fins[std::size_t(i)];
            fin.face = int(model.faces.size());
            Body::BuildFace face;
            face.surface = fin.surface;
            // Normale uscente: verso l'angolo se lo spigolo e' convesso (materiale tolto), dall'altra parte se concavo.
            const Edge &edge = body.edge(fin.edge);
            const double middle = 0.5 * (edge.range.lo + edge.range.hi);
            Vec3 d[4];
            fin.surface->evaluate(middle, 0.5, 1, d);
            const Vec3 normal = cross(d[Surface::derivativeIndex(1, 0, 1)], d[Surface::derivativeIndex(0, 1, 1)]);
            const Vec3 towardCorner = edge.curve->point(middle) - d[0];
            face.sense = (dot(normal, towardCorner) > 0.0) == convex;
            model.faces.push_back(std::move(face));
            const ChainVertex &a = vertices[std::size_t(i)], &b = vertices[std::size_t((i + 1) % vertexCount)];
            const int lo = fin.sense ? a.inPlanePoint : b.inPlanePoint, hi = fin.sense ? b.inPlanePoint : a.inPlanePoint;
            const int wlo = fin.sense ? a.wallPoint : b.wallPoint, whi = fin.sense ? b.wallPoint : a.wallPoint;
            fin.inPlaneEdge = model.addEdge(lo, hi, std::make_shared<BSplineCurve<3>>(fin.surface->vIsoCurve(1.0)), edge.range);
            fin.wallEdge = model.addEdge(wlo, whi, std::make_shared<BSplineCurve<3>>(fin.surface->vIsoCurve(0.0)), edge.range, fin.fitError > 1e-8 ? 2.0 * fin.fitError : 0.0);
        }
        // Connettori nei vertici: arco (o segmento) della sezione, o curva d'intersezione negli angoli vivi.
        for (int j = 0; j < vertexCount; ++j) {
            ChainVertex &cv = vertices[std::size_t(j)];
            const bool hasNext = chain.closed || j < n;
            const ChainFin &adjacent = hasNext ? chain.fins[std::size_t(j % n)] : chain.fins[std::size_t(n - 1)];
            const double u = hasNext ? adjacent.startParameter : adjacent.endParameter;
            if (cv.joint != Joint::Sharp) {
                cv.connector = model.addEdge(cv.wallPoint, cv.inPlanePoint, std::make_shared<BSplineCurve<3>>(adjacent.surface->uIsoCurve(u)), {0.0, 1.0});
                cv.connectorFromWall = true;
                continue;
            }
            // Angolo vivo: i due raccordi si tagliano lungo la loro intersezione, dal punto sul
            // fianco (comune: le traslate finiscono entrambe sullo spigolo verticale) a quello in T.
            ChainFin &previous = chain.fins[std::size_t((j - 1 + n) % n)];
            ChainFin &next = chain.fins[std::size_t(j % n)];
            const Vec3 corner = model.points[std::size_t(cv.wallPoint)];
            // Il vertice in T e' il punto comune esatto delle due parallele
            // (trasversali: Gauss-Newton ben condizionato), dai loro punti nel vertice.
            const Body::BuildEdge &inA = model.edges[std::size_t(previous.inPlaneEdge)], &inB = model.edges[std::size_t(next.inPlaneEdge)];
            double ua = previous.endParameter, ub = next.startParameter;
            for (int iteration = 0; iteration < 50; ++iteration) {
                Vec3 da[2], db[2];
                inA.curve->evaluate(ua, 1, da);
                inB.curve->evaluate(ub, 1, db);
                const Vec3 f = da[0] - db[0];
                const double a11 = dot(da[1], da[1]), a12 = -dot(da[1], db[1]), a22 = dot(db[1], db[1]);
                const double r1 = -dot(da[1], f), r2 = dot(db[1], f), det = a11 * a22 - a12 * a12;
                if (!(std::fabs(det) > 0.0)) break;
                const double du = (r1 * a22 - a12 * r2) / det, dv = (a11 * r2 - a12 * r1) / det;
                ua = std::clamp(ua + du, inA.range.lo, inA.range.hi);
                ub = std::clamp(ub + dv, inB.range.lo, inB.range.hi);
                if (std::fabs(du) + std::fabs(dv) < 1e-15 * (1.0 + std::fabs(ua) + std::fabs(ub))) break;
            }
            const Vec3 exact = 0.5 * (inA.curve->point(ua) + inB.curve->point(ub));
            if (distance(inA.curve->point(ua), inB.curve->point(ub)) > 1e-9 * scale)
                throw std::domain_error("blendEdges: le parallele dei raccordi nell'angolo non si incontrano (raggio troppo grande)");
            // I due raccordi si tagliano lungo la loro intersezione, dal punto sul fianco
            // (comune: le traslate finiscono entrambe sullo spigolo verticale) a quello in T.
            // Box stretto attorno all'angolo: oltre i due estremi le superfici escono dai
            // loro domini e il tracciamento non avrebbe senso.
            Box bounds;
            bounds.add(corner);
            bounds.add(exact);
            for (double v = 0.0; v <= 1.0; v += 0.125) {
                bounds.add(previous.surface->point(previous.endParameter, v));
                bounds.add(next.surface->point(next.startParameter, v));
            }
            bounds = bounds.padded(0.05 * size);
            SurfaceIntersectionOptions options;
            options.tolerance = 1e-7 * scale;
            // Seme interno, a meta' della sezione del primo raccordo: Newton su
            // S_prev(u, 1/2) = S_next(s, t) (le superfici vi si incontrano in modo trasversale).
            double su = 0.5 * (previous.endParameter + ua), ss = 0.5 * (next.startParameter + ub), st = 0.5;
            Vec3 seed = previous.surface->point(su, 0.5);
            for (int iteration = 0; iteration < 50; ++iteration) {
                Vec3 pa[4], pb[4];
                previous.surface->evaluate(su, 0.5, 1, pa);
                next.surface->evaluate(ss, st, 1, pb);
                const Vec3 f = pa[0] - pb[0];
                const Vec3 c0 = pa[Surface::derivativeIndex(1, 0, 1)], c1 = -pb[Surface::derivativeIndex(1, 0, 1)], c2 = -pb[Surface::derivativeIndex(0, 1, 1)];
                const double det = dot(c0, cross(c1, c2));
                if (!(std::fabs(det) > 0.0)) break;
                // J d = -f con la regola di Cramer.
                const double d0 = -dot(f, cross(c1, c2)) / det, d1 = -dot(c0, cross(f, c2)) / det, d2 = -dot(c0, cross(c1, f)) / det;
                su += d0;
                ss += d1;
                st += d2;
                seed = 0.5 * (previous.surface->point(su, 0.5) + next.surface->point(ss, st));
                if (norm(f) < 1e-13 * scale) break;
            }
            if (!(st > 0.0 && st < 1.0) || distance(previous.surface->point(su, 0.5), next.surface->point(ss, st)) > 1e-9 * scale)
                throw std::domain_error("blendEdges: raccordi nell'angolo senza punto comune (raggio troppo grande?)");
            // Per il tracciamento, i due raccordi rifatti solo sul tratto vicino
            // all'angolo: poche pezze (la ricerca dei semi le suddivide tutte), e
            // coincidono con quelli interi entro la tolleranza delle curve di controllo.
            auto local = [&](const ChainFin &fin, double at, double inner) {
                const Interval &full = body.edge(fin.edge).range;
                const double extra = 0.5 * std::fabs(at - inner);
                const double lo = at < inner ? at : inner - extra, hi = at < inner ? inner + extra : at;
                return fin.geometry->surface(fin.geometry->fit({std::max(full.lo, lo), std::min(full.hi, hi)}));
            };
            const std::shared_ptr<BSplineSurface> nearPrevious = local(previous, previous.endParameter, ua), nearNext = local(next, next.startParameter, ub);
            SurfaceIntersection hit;
            try {
                hit = intersectSurfaces(*nearPrevious, *nearNext, bounds, {seed}, options);
            } catch (const std::domain_error &failure) {
                const Vec3 p = body.vertex(cv.vertex).point;
                throw std::domain_error(std::string(failure.what()) + " (angolo in " + std::to_string(p.x()) + " " + std::to_string(p.y()) + " " + std::to_string(p.z()) + ")");
            }
            const IntersectionCurve *best = nullptr;
            double bestDistance = 1e300;
            for (const IntersectionCurve &c : hit.curves) {
                const double d = projectPoint(*c.curve, seed, c.range).distance;
                if (d < bestDistance) bestDistance = d, best = &c;
            }
            if (!best || bestDistance > 1e-6 * scale) throw std::domain_error("blendEdges: intersezione dei raccordi nell'angolo non trovata");
            // La curva, rifilata tra i due estremi. Nel punto in T i due raccordi sono
            // tangenti tra loro (entrambi tangenti a T): li' la curva e' determinata solo
            // al secondo ordine e il suo estremo si scosta un poco dal punto esatto:
            // vertici ed edge tolleranti per quello scarto.
            const CurveProjection atWall = projectPoint(*best->curve, corner, best->range), atTop = projectPoint(*best->curve, exact, best->range);
            const double gap = std::max(atWall.distance, atTop.distance);
            if (gap > 1e-3 * size || atWall.parameter == atTop.parameter)
                throw std::domain_error("blendEdges: la curva d'intersezione dei raccordi non collega il fianco alla faccia piana");
            const bool wallAtStart = atWall.parameter < atTop.parameter;
            const Interval range{std::min(atWall.parameter, atTop.parameter), std::max(atWall.parameter, atTop.parameter)};
            model.points[std::size_t(cv.inPlanePoint)] = exact;
            const double slack = std::max(2.0 * gap, 2.0 * best->deviation);
            if (slack > 1e-7) model.pointTolerance[std::size_t(cv.inPlanePoint)] = model.pointTolerance[std::size_t(cv.wallPoint)] = slack;
            cv.connector = model.addEdge(wallAtStart ? cv.wallPoint : cv.inPlanePoint, wallAtStart ? cv.inPlanePoint : cv.wallPoint,
                                         best->curve, range, slack > 1e-7 ? slack : 0.0);
            cv.connectorFromWall = wallAtStart;
            // Le parallele in T finiscono nel punto d'intersezione.
            for (ChainFin *fin : {&previous, &next}) {
                Body::BuildEdge &e = model.edges[std::size_t(fin->inPlaneEdge)];
                const double t = fin == &previous ? ua : ub;
                const bool atEnd = fin == &previous;  // la catena arriva nel vertice alla fine di previous
                const bool hi = fin->sense == atEnd;
                if (hi) e.range.hi = t;
                else e.range.lo = t;
            }
        }

        // Topologia: T e fianchi accorciati, spigoli verticali e di estremita' accorciati, facce nuove.
        const int planeFace = model.faceIndex.at(chain.plane.index);
        for (int i = 0; i < n; ++i) {
            const ChainFin &fin = chain.fins[std::size_t(i)];
            const int old = model.edgeIndex.at(fin.edge.index);
            model.replaceFin(planeFace, old, fin.inPlaneEdge);
            model.replaceFin(model.faceIndex.at(fin.wall.index), old, fin.wallEdge);
            model.edgeAlive[std::size_t(old)] = false;
        }
        for (int j = 0; j < vertexCount; ++j) {
            ChainVertex &cv = vertices[std::size_t(j)];
            const int vertex = model.vertexIndex.at(cv.vertex.index);
            if (cv.vertical >= 0) model.moveEnd(cv.vertical, vertex, cv.wallPoint);
            if (cv.joint == Joint::End) {
                model.moveEnd(cv.endEdge, vertex, cv.inPlanePoint);
                // Nel loop della faccia d'estremita' l'arco sta tra lo spigolo che vi arriva e quello che ne parte.
                const Body::BuildEdge &vertical = model.edges[std::size_t(cv.vertical)];
                bool verticalArrives = false;
                for (const auto &loop : model.faces[std::size_t(cv.endFace)].loops)
                    for (const Body::BuildFin &f : loop)
                        if (f.edge == cv.vertical) verticalArrives = (f.sense ? vertical.end : vertical.start) == cv.wallPoint;
                const int arrival = verticalArrives ? cv.wallPoint : cv.inPlanePoint;
                model.insertFinAfter(cv.endFace, arrival, cv.connector, verticalArrives == cv.connectorFromWall);
            }
        }
        for (int i = 0; i < n; ++i) {
            const ChainFin &fin = chain.fins[std::size_t(i)];
            const ChainVertex &a = vertices[std::size_t(i)], &b = vertices[std::size_t((i + 1) % vertexCount)];
            // Loop: parallela all'indietro, connettore all'inizio (da T al fianco), traslata in avanti, connettore alla fine.
            std::vector<Body::BuildFin> loop;
            loop.push_back({fin.inPlaneEdge, !fin.sense, nullptr, 0.0});
            loop.push_back({a.connector, !a.connectorFromWall, nullptr, 0.0});
            loop.push_back({fin.wallEdge, fin.sense, nullptr, 0.0});
            loop.push_back({b.connector, b.connectorFromWall, nullptr, 0.0});
            model.faces[std::size_t(fin.face)].loops.push_back(std::move(loop));
        }
    }
    Body result = model.build();
    computePCurves(result);
    const std::vector<CheckIssue> issues = checkBody(result);
    if (!issues.empty()) throw std::domain_error("blendEdges: raccordo non valido (" + describe(issues.front().code) + ": " + issues.front().message + ")");
    return result;
}

}
