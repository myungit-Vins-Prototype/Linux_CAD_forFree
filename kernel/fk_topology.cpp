#include "fk_topology.h"

#include <algorithm>
#include <stdexcept>

#include "fk_curve_ops.h"

namespace ForgeCad::Kernel {
namespace {

template <class T, class Tag>
T &entity(std::vector<T> &store, Id<Tag> id, const char *what) {
    if (id.index < 0 || id.index >= int(store.size()) || !store[id.index].alive)
        throw std::out_of_range(std::string("Body: ") + what + " non valido");
    return store[id.index];
}

template <class T, class Tag>
const T &entity(const std::vector<T> &store, Id<Tag> id, const char *what) {
    if (id.index < 0 || id.index >= int(store.size()) || !store[id.index].alive)
        throw std::out_of_range(std::string("Body: ") + what + " non valido");
    return store[id.index];
}

template <class T, class Tag>
bool isAlive(const std::vector<T> &store, Id<Tag> id) {
    return id.index >= 0 && id.index < int(store.size()) && store[id.index].alive;
}

template <class IdT, class T>
std::vector<IdT> aliveIds(const std::vector<T> &store) {
    std::vector<IdT> result;
    for (int i = 0; i < int(store.size()); ++i)
        if (store[i].alive) result.push_back(IdT(i));
    return result;
}

template <class IdT>
void eraseValue(std::vector<IdT> &values, IdT value) {
    values.erase(std::remove(values.begin(), values.end(), value), values.end());
}

}

Body::Body() { regions_.push_back(Region{}); }  // region 0: esterno

const Vertex &Body::vertex(VertexId id) const { return entity(vertices_, id, "vertice"); }
Vertex &Body::vertex(VertexId id) { return entity(vertices_, id, "vertice"); }
const Edge &Body::edge(EdgeId id) const { return entity(edges_, id, "edge"); }
Edge &Body::edge(EdgeId id) { return entity(edges_, id, "edge"); }
const Fin &Body::fin(FinId id) const { return entity(fins_, id, "fin"); }
Fin &Body::fin(FinId id) { return entity(fins_, id, "fin"); }
const Loop &Body::loop(LoopId id) const { return entity(loops_, id, "loop"); }
Loop &Body::loop(LoopId id) { return entity(loops_, id, "loop"); }
const Face &Body::face(FaceId id) const { return entity(faces_, id, "faccia"); }
Face &Body::face(FaceId id) { return entity(faces_, id, "faccia"); }
const Shell &Body::shell(ShellId id) const { return entity(shells_, id, "shell"); }
Shell &Body::shell(ShellId id) { return entity(shells_, id, "shell"); }
const Region &Body::region(RegionId id) const { return entity(regions_, id, "region"); }
Region &Body::region(RegionId id) { return entity(regions_, id, "region"); }

bool Body::contains(VertexId id) const { return isAlive(vertices_, id); }
bool Body::contains(EdgeId id) const { return isAlive(edges_, id); }
bool Body::contains(FinId id) const { return isAlive(fins_, id); }
bool Body::contains(LoopId id) const { return isAlive(loops_, id); }
bool Body::contains(FaceId id) const { return isAlive(faces_, id); }
bool Body::contains(ShellId id) const { return isAlive(shells_, id); }
bool Body::contains(RegionId id) const { return isAlive(regions_, id); }

std::vector<VertexId> Body::vertices() const { return aliveIds<VertexId>(vertices_); }
std::vector<EdgeId> Body::edges() const { return aliveIds<EdgeId>(edges_); }
std::vector<FinId> Body::fins() const { return aliveIds<FinId>(fins_); }
std::vector<LoopId> Body::loops() const { return aliveIds<LoopId>(loops_); }
std::vector<FaceId> Body::faces() const { return aliveIds<FaceId>(faces_); }
std::vector<ShellId> Body::shells() const { return aliveIds<ShellId>(shells_); }
std::vector<RegionId> Body::regions() const { return aliveIds<RegionId>(regions_); }

TopologyCounts Body::counts() const {
    TopologyCounts result;
    result.vertices = int(vertices().size());
    result.edges = int(edges().size());
    result.fins = int(fins().size());
    result.loops = int(loops().size());
    result.faces = int(faces().size());
    result.shells = int(shells().size());
    result.regions = int(regions().size());
    return result;
}

FinId Body::otherFin(FinId f) const {
    const Edge &e = edge(fin(f).edge);
    return e.forward == f ? e.backward : e.forward;
}

std::vector<FinId> Body::loopFins(LoopId l) const {
    std::vector<FinId> result;
    const FinId first = loop(l).first;
    if (!first.valid()) return result;
    FinId f = first;
    do {
        result.push_back(f);
        f = fin(f).next;
        if (result.size() > fins_.size()) throw std::logic_error("Body::loopFins: ciclo non chiuso");
    } while (f != first);
    return result;
}

FinId Body::findFin(LoopId l, VertexId v) const {
    for (FinId f : loopFins(l))
        if (fin(f).vertex == v) return f;
    return FinId();
}

Vec3 Body::finPoint(FinId f, double s) const {
    const Fin &fn = fin(f);
    const Edge &e = edge(fn.edge);
    const double along = fn.sense ? s : 1.0 - s;
    if (e.curve) return e.curve->point(e.range.lo + along * e.range.length());
    const Vec3 a = vertex(fn.vertex).point, b = vertex(finEnd(f)).point;
    return a + s * (b - a);
}

void Body::reverseEdge(EdgeId e) {
    Edge &ed = edge(e);
    std::swap(ed.forward, ed.backward);
    fin(ed.forward).sense = true;
    fin(ed.backward).sense = false;
    if (ed.curve) {
        ed.curve = reversedCurve<3>(ed.curve);
        ed.range = {-ed.range.hi, -ed.range.lo};
    }
    for (FinId f : {ed.forward, ed.backward})
        if (fin(f).pcurve) fin(f).pcurve = reversedCurve<2>(fin(f).pcurve);
}

// --- Creazione ed eliminazione elementare ---------------------------------

VertexId Body::newVertex(const Vec3 &point) {
    Vertex v;
    v.point = point;
    vertices_.push_back(v);
    return VertexId(int(vertices_.size()) - 1);
}

EdgeId Body::newEdge() {
    edges_.push_back(Edge{});
    return EdgeId(int(edges_.size()) - 1);
}

FinId Body::newFin(LoopId l, EdgeId e, VertexId v, bool sense) {
    Fin f;
    f.loop = l;
    f.edge = e;
    f.vertex = v;
    f.sense = sense;
    fins_.push_back(f);
    const FinId id(int(fins_.size()) - 1);
    fins_.back().next = fins_.back().previous = id;
    return id;
}

LoopId Body::newLoop(FaceId faceId) {
    Loop l;
    l.face = faceId;
    loops_.push_back(l);
    const LoopId id(int(loops_.size()) - 1);
    face(faceId).loops.push_back(id);
    return id;
}

FaceId Body::newFace(ShellId shellId) {
    Face f;
    f.shell = shellId;
    faces_.push_back(f);
    const FaceId id(int(faces_.size()) - 1);
    shell(shellId).faces.push_back(id);
    return id;
}

ShellId Body::newShell(RegionId regionId) {
    Shell s;
    s.region = regionId;
    shells_.push_back(s);
    const ShellId id(int(shells_.size()) - 1);
    region(regionId).shells.push_back(id);
    return id;
}

RegionId Body::newRegion(bool solid) {
    Region r;
    r.solid = solid;
    regions_.push_back(r);
    return RegionId(int(regions_.size()) - 1);
}

void Body::link(FinId from, FinId to) {
    fin(from).next = to;
    fin(to).previous = from;
}

int Body::finsAt(VertexId v) const {
    int count = 0;
    for (const Fin &f : fins_)
        if (f.alive && f.vertex == v) ++count;
    return count;
}

// --- Costruzione diretta -----------------------------------------------------

Body Body::build(const std::vector<Vec3> &points, const std::vector<BuildEdge> &edgeSpecs, const std::vector<BuildFace> &faceSpecs) {
    // Shell: componenti connesse delle facce (union-find sugli edge).
    std::vector<int> parent(faceSpecs.size());
    for (std::size_t i = 0; i < parent.size(); ++i) parent[i] = int(i);
    auto root = [&](int i) {
        while (parent[i] != i) i = parent[i] = parent[parent[i]];
        return i;
    };
    std::vector<int> firstFace(edgeSpecs.size(), -1), forwardCount(edgeSpecs.size(), 0), backwardCount(edgeSpecs.size(), 0);
    for (std::size_t f = 0; f < faceSpecs.size(); ++f)
        for (const auto &loopSpec : faceSpecs[f].loops)
            for (const BuildFin &finSpec : loopSpec) {
                if (finSpec.edge < 0 || finSpec.edge >= int(edgeSpecs.size())) throw std::invalid_argument("Body::build: edge inesistente");
                (finSpec.sense ? forwardCount : backwardCount)[finSpec.edge]++;
                if (firstFace[finSpec.edge] < 0) firstFace[finSpec.edge] = int(f);
                else parent[root(int(f))] = root(firstFace[finSpec.edge]);
            }
    for (std::size_t e = 0; e < edgeSpecs.size(); ++e)
        if (forwardCount[e] != 1 || backwardCount[e] != 1)
            throw std::invalid_argument("Body::build: l'edge " + std::to_string(e) + " non ha due fin opposte");

    Body body;
    const RegionId region = body.newRegion(true);
    std::vector<ShellId> shellOf(faceSpecs.size());
    std::vector<int> shellIndex(faceSpecs.size(), -1);
    for (std::size_t f = 0; f < faceSpecs.size(); ++f) {
        const int r = root(int(f));
        if (shellIndex[r] < 0) shellIndex[r] = body.newShell(region).index;
        shellOf[f] = ShellId(shellIndex[r]);
    }
    std::vector<VertexId> vertexIds;
    for (const Vec3 &p : points) vertexIds.push_back(body.newVertex(p));
    std::vector<EdgeId> edgeIds;
    for (const BuildEdge &spec : edgeSpecs) {
        const EdgeId e = body.newEdge();
        body.edge(e).curve = spec.curve;
        body.edge(e).range = spec.range;
        body.edge(e).tolerance = spec.tolerance;
        edgeIds.push_back(e);
    }
    for (std::size_t f = 0; f < faceSpecs.size(); ++f) {
        const FaceId face = body.newFace(shellOf[f]);
        body.face(face).surface = faceSpecs[f].surface;
        body.face(face).sense = faceSpecs[f].sense;
        for (const auto &loopSpec : faceSpecs[f].loops) {
            const LoopId l = body.newLoop(face);
            FinId first, previous;
            for (const BuildFin &finSpec : loopSpec) {
                const BuildEdge &spec = edgeSpecs[finSpec.edge];
                const VertexId start = vertexIds[finSpec.sense ? spec.start : spec.end];
                const FinId fin = body.newFin(l, edgeIds[finSpec.edge], start, finSpec.sense);
                (finSpec.sense ? body.edge(edgeIds[finSpec.edge]).forward : body.edge(edgeIds[finSpec.edge]).backward) = fin;
                body.fin(fin).pcurve = finSpec.pcurve;
                body.fin(fin).pcurveTolerance = finSpec.pcurveTolerance;
                if (!first.valid()) first = fin;
                else body.link(previous, fin);
                previous = fin;
            }
            if (!first.valid()) throw std::invalid_argument("Body::build: loop vuoto");
            body.link(previous, first);
            body.loop(l).first = first;
        }
    }
    return body;
}

// --- Operatori di Eulero ---------------------------------------------------

Body::MvfsResult Body::mvfs(const Vec3 &point) {
    MvfsResult r;
    r.region = newRegion(true);
    r.shell = newShell(r.region);
    r.face = newFace(r.shell);
    r.loop = newLoop(r.face);
    r.vertex = newVertex(point);
    loop(r.loop).isolatedVertex = r.vertex;
    return r;
}

void Body::kvfs(ShellId s) {
    const Shell &sh = shell(s);
    if (sh.faces.size() != 1) throw std::logic_error("kvfs: la shell non e' minima");
    const FaceId f = sh.faces.front();
    if (face(f).loops.size() != 1) throw std::logic_error("kvfs: la shell non e' minima");
    const LoopId l = face(f).loops.front();
    if (!loop(l).isolatedVertex.valid()) throw std::logic_error("kvfs: la shell non e' minima");
    vertex(loop(l).isolatedVertex).alive = false;
    loop(l).alive = false;
    face(f).alive = false;
    const RegionId r = sh.region;
    shell(s).alive = false;
    eraseValue(region(r).shells, s);
    if (r != exteriorRegion() && region(r).shells.empty()) region(r).alive = false;
}

Body::MevResult Body::mev(LoopId l, const Vec3 &point) {
    const VertexId v1 = loop(l).isolatedVertex;
    if (!v1.valid()) throw std::logic_error("mev: il loop non e' un vertice isolato");
    const VertexId v2 = newVertex(point);
    const EdgeId e = newEdge();
    const FinId a = newFin(l, e, v1, true), b = newFin(l, e, v2, false);
    link(a, b);
    link(b, a);
    edge(e).forward = a;
    edge(e).backward = b;
    loop(l).isolatedVertex = VertexId();
    loop(l).first = a;
    return {e, v2};
}

Body::MevResult Body::mev(FinId before, const Vec3 &point) {
    const LoopId l = fin(before).loop;
    const VertexId v1 = fin(before).vertex;
    const FinId p = fin(before).previous;
    const VertexId v2 = newVertex(point);
    const EdgeId e = newEdge();
    const FinId a = newFin(l, e, v1, true), b = newFin(l, e, v2, false);
    link(p, a);
    link(a, b);
    link(b, before);
    edge(e).forward = a;
    edge(e).backward = b;
    return {e, v2};
}

void Body::kev(EdgeId e) {
    const FinId a = edge(e).forward, b = edge(e).backward;
    const LoopId l = fin(a).loop;
    if (fin(b).loop != l) throw std::logic_error("kev: le fin dell'edge sono in loop diversi");
    const bool aThenB = fin(a).next == b, bThenA = fin(b).next == a;
    // La punta dello sperone: il vertice da cui la fin torna subito indietro.
    VertexId tip;
    if (aThenB && finsAt(fin(b).vertex) == 1) tip = fin(b).vertex;
    else if (bThenA && finsAt(fin(a).vertex) == 1) tip = fin(a).vertex;
    else throw std::logic_error("kev: l'edge non e' uno sperone");

    if (aThenB && bThenA) {
        loop(l).first = FinId();
        loop(l).isolatedVertex = tip == fin(b).vertex ? fin(a).vertex : fin(b).vertex;
    } else if (tip == fin(b).vertex) {
        const FinId after = fin(b).next;
        link(fin(a).previous, after);
        loop(l).first = after;
    } else {
        const FinId after = fin(a).next;
        link(fin(b).previous, after);
        loop(l).first = after;
    }
    fin(a).alive = fin(b).alive = false;
    edge(e).alive = false;
    vertex(tip).alive = false;
}

Body::MefResult Body::mef(FinId first, FinId second) {
    const LoopId l1 = fin(first).loop;
    if (fin(second).loop != l1) throw std::logic_error("mef: le fin sono in loop diversi");
    const VertexId v1 = fin(first).vertex, v2 = fin(second).vertex;
    const FaceId newF = newFace(face(loop(l1).face).shell);
    const LoopId l2 = newLoop(newF);
    const EdgeId e = newEdge();
    const FinId a = newFin(l1, e, v1, true), b = newFin(l2, e, v2, false);
    edge(e).forward = a;
    edge(e).backward = b;
    if (first == second) {
        link(fin(first).previous, a);
        link(a, first);
        link(b, b);
    } else {
        const FinId p1 = fin(first).previous, p2 = fin(second).previous;
        link(p1, a);
        link(a, second);
        link(p2, b);
        link(b, first);
        for (FinId f = first; f != b; f = fin(f).next) fin(f).loop = l2;
    }
    loop(l1).first = a;
    loop(l2).first = b;
    return {e, newF, l2};
}

Body::MefResult Body::mef(LoopId l) {
    const VertexId v = loop(l).isolatedVertex;
    if (!v.valid()) throw std::logic_error("mef: il loop non e' un vertice isolato");
    const FaceId newF = newFace(face(loop(l).face).shell);
    const LoopId l2 = newLoop(newF);
    const EdgeId e = newEdge();
    const FinId a = newFin(l, e, v, true), b = newFin(l2, e, v, false);
    edge(e).forward = a;
    edge(e).backward = b;
    loop(l).isolatedVertex = VertexId();
    loop(l).first = a;
    loop(l2).first = b;
    return {e, newF, l2};
}

void Body::kef(EdgeId e) {
    const FinId a = edge(e).forward, b = edge(e).backward;
    const LoopId l1 = fin(a).loop, l2 = fin(b).loop;
    const FaceId f1 = loop(l1).face, f2 = loop(l2).face;
    if (f1 == f2) throw std::logic_error("kef: le fin sono nella stessa faccia (usare kemr)");
    const FinId pa = fin(a).previous, na = fin(a).next, pb = fin(b).previous, nb = fin(b).next;
    const bool aAlone = na == a, bAlone = nb == b;
    FinId keep;
    if (aAlone && bAlone) {
        loop(l1).isolatedVertex = fin(a).vertex;
    } else if (bAlone) {
        link(pa, na);
        keep = na;
    } else if (aAlone) {
        link(pb, nb);
        keep = nb;
    } else {
        link(pa, nb);
        link(pb, na);
        keep = na;
    }
    if (!bAlone)
        for (FinId f = nb;; f = fin(f).next) {
            fin(f).loop = l1;
            if (f == pb) break;
        }
    loop(l1).first = keep;
    // Gli altri loop della faccia eliminata passano alla faccia che resta.
    for (LoopId other : face(f2).loops) {
        if (other == l2) continue;
        loop(other).face = f1;
        face(f1).loops.push_back(other);
    }
    loop(l2).alive = false;
    eraseValue(shell(face(f2).shell).faces, f2);
    face(f2).alive = false;
    fin(a).alive = fin(b).alive = false;
    edge(e).alive = false;
}

LoopId Body::kemr(EdgeId e) {
    const FinId a = edge(e).forward, b = edge(e).backward;
    const LoopId l = fin(a).loop;
    if (fin(b).loop != l) throw std::logic_error("kemr: le fin dell'edge sono in loop diversi");
    const FinId na = fin(a).next, pa = fin(a).previous, nb = fin(b).next, pb = fin(b).previous;
    const bool xEmpty = na == b, yEmpty = nb == a;
    const LoopId ring = newLoop(loop(l).face);
    // Tratto X (tra a e b) -> nuovo anello.
    if (xEmpty) {
        loop(ring).isolatedVertex = fin(b).vertex;
    } else {
        link(pb, na);
        for (FinId f = na;; f = fin(f).next) {
            fin(f).loop = ring;
            if (f == pb) break;
        }
        loop(ring).first = na;
    }
    // Tratto Y (tra b e a) -> resta nel loop.
    if (yEmpty) {
        loop(l).first = FinId();
        loop(l).isolatedVertex = fin(a).vertex;
    } else {
        link(pa, nb);
        loop(l).first = nb;
    }
    fin(a).alive = fin(b).alive = false;
    edge(e).alive = false;
    return ring;
}

EdgeId Body::mekr(LoopId l1, FinId f1, LoopId l2, FinId f2) {
    if (l1 == l2) throw std::logic_error("mekr: serve due loop diversi");
    if (loop(l1).face != loop(l2).face) throw std::logic_error("mekr: i loop sono in facce diverse");
    if (f1.valid() ? fin(f1).loop != l1 : !loop(l1).isolatedVertex.valid())
        throw std::logic_error("mekr: posizione non valida nel primo loop");
    if (f2.valid() ? fin(f2).loop != l2 : !loop(l2).isolatedVertex.valid())
        throw std::logic_error("mekr: posizione non valida nel secondo loop");
    const VertexId v1 = f1.valid() ? fin(f1).vertex : loop(l1).isolatedVertex;
    const VertexId v2 = f2.valid() ? fin(f2).vertex : loop(l2).isolatedVertex;
    const EdgeId e = newEdge();
    const FinId a = newFin(l1, e, v1, true), b = newFin(l1, e, v2, false);
    edge(e).forward = a;
    edge(e).backward = b;
    // ... p1 -> a -> [secondo loop da f2] -> b -> f1 ...
    if (f2.valid()) {
        const FinId last2 = fin(f2).previous;
        for (FinId f = f2;; f = fin(f).next) {
            fin(f).loop = l1;
            if (f == last2) break;
        }
        link(a, f2);
        link(last2, b);
    } else {
        link(a, b);
    }
    if (f1.valid()) {
        const FinId p1 = fin(f1).previous;
        link(p1, a);
        link(b, f1);
    } else {
        link(b, a);
        loop(l1).isolatedVertex = VertexId();
    }
    loop(l1).first = a;
    eraseValue(face(loop(l2).face).loops, l2);
    loop(l2).alive = false;
    return e;
}

void Body::kfmrh(FaceId kill, FaceId keep) {
    if (kill == keep) throw std::logic_error("kfmrh: stessa faccia");
    if (face(kill).loops.size() != 1) throw std::logic_error("kfmrh: la faccia da eliminare deve avere un solo loop");
    if (face(kill).shell != face(keep).shell) throw std::logic_error("kfmrh: facce in shell diverse");
    const LoopId l = face(kill).loops.front();
    loop(l).face = keep;
    face(keep).loops.push_back(l);
    eraseValue(shell(face(kill).shell).faces, kill);
    face(kill).alive = false;
}

FaceId Body::mfkrh(LoopId ring) {
    const FaceId f = loop(ring).face;
    if (face(f).loops.size() < 2) throw std::logic_error("mfkrh: il loop e' l'unico della faccia");
    eraseValue(face(f).loops, ring);
    Face created;
    created.shell = face(f).shell;
    created.loops = {ring};
    faces_.push_back(created);
    const FaceId id(int(faces_.size()) - 1);
    shell(created.shell).faces.push_back(id);
    loop(ring).face = id;
    return id;
}

Body::SemvResult Body::semv(EdgeId e, const Vec3 &point, double parameter) {
    const FinId a = edge(e).forward, b = edge(e).backward;
    const VertexId end = fin(b).vertex;
    const VertexId middle = newVertex(point);
    const EdgeId e2 = newEdge();
    const FinId a2 = newFin(fin(a).loop, e2, middle, true);
    const FinId b2 = newFin(fin(b).loop, e2, end, false);
    edge(e2).forward = a2;
    edge(e2).backward = b2;
    {
        const FinId na = fin(a).next;
        link(a, a2);
        link(a2, na);
    }
    {
        const FinId pb = fin(b).previous;  // dopo il blocco sopra, se na era b vale a2
        link(pb, b2);
        link(b2, b);
    }
    fin(b).vertex = middle;
    // Le SP-curve hanno il parametro dell'edge: le due meta' le condividono.
    fin(a2).pcurve = fin(a).pcurve;
    fin(a2).pcurveTolerance = fin(a).pcurveTolerance;
    fin(b2).pcurve = fin(b).pcurve;
    fin(b2).pcurveTolerance = fin(b).pcurveTolerance;
    Edge &first = edge(e);
    if (first.curve) {
        Edge &second = edge(e2);
        second.curve = first.curve;
        second.range = {parameter, first.range.hi};
        second.tolerance = first.tolerance;
        first.range.hi = parameter;
    }
    return {middle, e2};
}

void Body::jekv(EdgeId e2) {
    const FinId a2 = edge(e2).forward, b2 = edge(e2).backward;
    const VertexId middle = fin(a2).vertex;
    const FinId a = fin(a2).previous, b = fin(b2).next;
    const EdgeId e = fin(a).edge;
    if (e == e2 || fin(b).edge != e || edge(e).forward != a || edge(e).backward != b)
        throw std::logic_error("jekv: l'edge non segue un altro edge nello stesso verso");
    if (finsAt(middle) != 2) throw std::logic_error("jekv: il vertice intermedio ha altri edge");
    const FinId na2 = fin(a2).next, pb2 = fin(b2).previous;
    if (na2 == b2) {
        link(a, b);
    } else {
        link(a, na2);
        link(pb2, b);
    }
    fin(b).vertex = fin(b2).vertex;
    if (loop(fin(a2).loop).first == a2) loop(fin(a2).loop).first = a;
    if (loop(fin(b2).loop).first == b2) loop(fin(b2).loop).first = b;
    Edge &first = edge(e);
    const Edge &second = edge(e2);
    if (first.curve && first.curve == second.curve) first.range.hi = second.range.hi;
    fin(a2).alive = fin(b2).alive = false;
    edge(e2).alive = false;
    vertex(middle).alive = false;
}

}
