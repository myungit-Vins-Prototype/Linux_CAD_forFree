#include "fk_tessellate.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>
#include <map>
#include <stdexcept>

#include "fk_pcurve.h"

namespace ForgeCad::Kernel {

// --- Triangolazione a orecchie (algoritmo di earcut, Mapbox) ---------------------

namespace {

struct EarNode {
    int index;
    double x, y;
    EarNode *prev = nullptr, *next = nullptr;
    bool steiner = false;
};

class EarCut {
public:
    std::vector<std::array<int, 3>> triangles;

    void run(const std::vector<Vec2> &outer, const std::vector<std::vector<Vec2>> &holes) {
        int offset = 0;
        EarNode *outerNode = linkedList(outer, offset, true);
        offset += int(outer.size());
        if (!outerNode || outerNode->next == outerNode->prev) return;
        if (!holes.empty()) {
            std::vector<EarNode *> queue;
            for (const std::vector<Vec2> &hole : holes) {
                EarNode *list = linkedList(hole, offset, false);
                offset += int(hole.size());
                if (!list) continue;
                if (list == list->next) list->steiner = true;
                queue.push_back(leftmost(list));
            }
            std::sort(queue.begin(), queue.end(), [](const EarNode *a, const EarNode *b) {
                return a->x < b->x || (a->x == b->x && a->y < b->y);
            });
            for (EarNode *hole : queue) outerNode = eliminateHole(hole, outerNode);
        }
        earcutLinked(outerNode, 0);
    }

private:
    std::deque<EarNode> nodes_;

    // Area con segno del triangolo: positiva se p, q, r girano in senso antiorario.
    static double orient(const EarNode *p, const EarNode *q, const EarNode *r) {
        return (q->x - p->x) * (r->y - q->y) - (q->y - p->y) * (r->x - q->x);
    }
    static bool equals(const EarNode *a, const EarNode *b) { return a->x == b->x && a->y == b->y; }
    static bool pointInTriangle(double ax, double ay, double bx, double by, double cx, double cy, double px, double py) {
        return (cx - px) * (ay - py) >= (ax - px) * (cy - py) && (ax - px) * (by - py) >= (bx - px) * (ay - py)
            && (bx - px) * (cy - py) >= (cx - px) * (by - py);
    }

    EarNode *insert(int index, double x, double y, EarNode *last) {
        nodes_.push_back(EarNode{index, x, y});
        EarNode *p = &nodes_.back();
        if (!last) {
            p->prev = p->next = p;
        } else {
            p->next = last->next;
            p->prev = last;
            last->next->prev = p;
            last->next = p;
        }
        return p;
    }
    static void remove(EarNode *p) {
        p->next->prev = p->prev;
        p->prev->next = p->next;
    }

    EarNode *linkedList(const std::vector<Vec2> &ring, int offset, bool counterClockwise) {
        if (ring.empty()) return nullptr;
        double area = 0.0;
        for (std::size_t i = 0, j = ring.size() - 1; i < ring.size(); j = i++) area += cross(ring[j], ring[i]);
        EarNode *last = nullptr;
        if ((area > 0.0) == counterClockwise) {
            for (std::size_t i = 0; i < ring.size(); ++i) last = insert(offset + int(i), ring[i].x(), ring[i].y(), last);
        } else {
            for (std::size_t i = ring.size(); i-- > 0;) last = insert(offset + int(i), ring[i].x(), ring[i].y(), last);
        }
        if (last && equals(last, last->next)) {
            EarNode *next = last->next;
            remove(last);
            last = next;
        }
        return last;
    }

    // Toglie i punti doppi. A differenza di earcut i punti allineati restano:
    // sono campioni di un bordo che in 3D non e' rettilineo.
    EarNode *filterPoints(EarNode *start, EarNode *end = nullptr) {
        if (!start) return start;
        if (!end) end = start;
        EarNode *p = start;
        bool again;
        do {
            again = false;
            if (!p->steiner && equals(p, p->next)) {
                remove(p);
                p = end = p->prev;
                if (p == p->next) break;
                again = true;
            } else {
                p = p->next;
            }
        } while (again || p != end);
        return end;
    }

    void earcutLinked(EarNode *ear, int pass) {
        if (!ear) return;
        EarNode *stop = ear;
        while (ear->prev != ear->next) {
            EarNode *prev = ear->prev, *next = ear->next;
            if (isEar(ear)) {
                triangles.push_back({prev->index, ear->index, next->index});
                remove(ear);
                // Saltare il vertice successivo da' meno triangoli sottili.
                ear = next->next;
                stop = next->next;
                continue;
            }
            ear = next;
            if (ear == stop) {
                if (pass == 0) {
                    earcutLinked(filterPoints(ear), 1);
                } else if (pass == 1) {
                    ear = cureLocalIntersections(filterPoints(ear));
                    earcutLinked(ear, 2);
                } else {
                    splitEarcut(ear);
                }
                break;
            }
        }
    }

    bool isEar(const EarNode *ear) const {
        const EarNode *a = ear->prev, *b = ear, *c = ear->next;
        // Riflesso o piatto: i campioni di un bordo isoparametrico sono allineati
        // nel piano (u, v) a meno dell'arrotondamento, ma in 3D non lo sono.
        const double ab = std::hypot(b->x - a->x, b->y - a->y), bc = std::hypot(c->x - b->x, c->y - b->y);
        if (orient(a, b, c) <= 1e-10 * ab * bc) return false;
        for (const EarNode *p = c->next; p != a; p = p->next) {
            if (equals(p, a) || equals(p, b) || equals(p, c)) continue;
            if (pointInTriangle(a->x, a->y, b->x, b->y, c->x, c->y, p->x, p->y) && orient(p->prev, p, p->next) <= 0.0)
                return false;
        }
        return true;
    }

    static bool onSegment(const EarNode *p, const EarNode *q, const EarNode *r) {
        return q->x <= std::max(p->x, r->x) && q->x >= std::min(p->x, r->x) && q->y <= std::max(p->y, r->y)
            && q->y >= std::min(p->y, r->y);
    }
    static int sign(double value) { return (value > 0.0) - (value < 0.0); }
    static bool intersects(const EarNode *p1, const EarNode *q1, const EarNode *p2, const EarNode *q2) {
        const int o1 = sign(orient(p1, q1, p2)), o2 = sign(orient(p1, q1, q2));
        const int o3 = sign(orient(p2, q2, p1)), o4 = sign(orient(p2, q2, q1));
        if (o1 != o2 && o3 != o4) return true;
        if (o1 == 0 && onSegment(p1, p2, q1)) return true;
        if (o2 == 0 && onSegment(p1, q2, q1)) return true;
        if (o3 == 0 && onSegment(p2, p1, q2)) return true;
        if (o4 == 0 && onSegment(p2, q1, q2)) return true;
        return false;
    }
    static bool intersectsPolygon(const EarNode *a, const EarNode *b) {
        const EarNode *p = a;
        do {
            if (p->index != a->index && p->next->index != a->index && p->index != b->index && p->next->index != b->index
                && intersects(p, p->next, a, b))
                return true;
            p = p->next;
        } while (p != a);
        return false;
    }
    // La diagonale a-b parte da a verso l'interno del poligono.
    static bool locallyInside(const EarNode *a, const EarNode *b) {
        return orient(a->prev, a, a->next) > 0.0 ? orient(a, b, a->next) <= 0.0 && orient(a, a->prev, b) <= 0.0
                                                 : orient(a, b, a->prev) > 0.0 || orient(a, a->next, b) > 0.0;
    }
    static bool middleInside(const EarNode *a, const EarNode *b) {
        const EarNode *p = a;
        bool inside = false;
        const double px = 0.5 * (a->x + b->x), py = 0.5 * (a->y + b->y);
        do {
            if (((p->y > py) != (p->next->y > py)) && p->next->y != p->y
                && px < (p->next->x - p->x) * (py - p->y) / (p->next->y - p->y) + p->x)
                inside = !inside;
            p = p->next;
        } while (p != a);
        return inside;
    }
    static bool isValidDiagonal(const EarNode *a, const EarNode *b) {
        return a->next->index != b->index && a->prev->index != b->index && !intersectsPolygon(a, b)
            && ((locallyInside(a, b) && locallyInside(b, a) && middleInside(a, b)
                 && (orient(a->prev, a, b->prev) != 0.0 || orient(a, b->prev, b) != 0.0))
                || (equals(a, b) && orient(a->prev, a, a->next) < 0.0 && orient(b->prev, b, b->next) < 0.0));
    }

    EarNode *cureLocalIntersections(EarNode *start) {
        EarNode *p = start;
        do {
            EarNode *a = p->prev, *b = p->next->next;
            if (!equals(a, b) && orient(a, p, b) > 1e-10 * std::hypot(p->x - a->x, p->y - a->y) * std::hypot(b->x - p->x, b->y - p->y)
                && intersects(a, p, p->next, b) && locallyInside(a, b) && locallyInside(b, a)) {
                triangles.push_back({a->index, p->index, b->index});
                remove(p);
                remove(p->next);
                p = start = b;
            }
            p = p->next;
        } while (p != start);
        return filterPoints(p);
    }

    void splitEarcut(EarNode *start) {
        EarNode *a = start;
        do {
            EarNode *b = a->next->next;
            while (b != a->prev) {
                if (a->index != b->index && isValidDiagonal(a, b)) {
                    EarNode *c = splitPolygon(a, b);
                    a = filterPoints(a, a->next);
                    c = filterPoints(c, c->next);
                    earcutLinked(a, 0);
                    earcutLinked(c, 0);
                    return;
                }
                b = b->next;
            }
            a = a->next;
        } while (a != start);
    }

    // Collega a e b con una diagonale: due poligoni (a..b e b..a).
    EarNode *splitPolygon(EarNode *a, EarNode *b) {
        nodes_.push_back(EarNode{a->index, a->x, a->y});
        EarNode *a2 = &nodes_.back();
        nodes_.push_back(EarNode{b->index, b->x, b->y});
        EarNode *b2 = &nodes_.back();
        EarNode *an = a->next, *bp = b->prev;
        a->next = b;
        b->prev = a;
        a2->next = an;
        an->prev = a2;
        b2->next = a2;
        a2->prev = b2;
        bp->next = b2;
        b2->prev = bp;
        return b2;
    }

    static EarNode *leftmost(EarNode *start) {
        EarNode *p = start, *best = start;
        do {
            if (p->x < best->x || (p->x == best->x && p->y < best->y)) best = p;
            p = p->next;
        } while (p != start);
        return best;
    }

    static bool sectorContainsSector(const EarNode *m, const EarNode *p) {
        return orient(m->prev, m, p->prev) > 0.0 && orient(p->next, m, m->next) > 0.0;
    }

    // Vertice del contorno esterno visibile dal punto piu' a sinistra del
    // foro (algoritmo di Eberly).
    static EarNode *findHoleBridge(const EarNode *hole, EarNode *outerNode) {
        EarNode *p = outerNode, *m = nullptr;
        const double hx = hole->x, hy = hole->y;
        double qx = -std::numeric_limits<double>::infinity();
        do {
            if (hy <= p->y && hy >= p->next->y && p->next->y != p->y) {
                const double x = p->x + (hy - p->y) * (p->next->x - p->x) / (p->next->y - p->y);
                if (x <= hx && x > qx) {
                    qx = x;
                    m = p->x < p->next->x ? p : p->next;
                    if (x == hx) return m;
                }
            }
            p = p->next;
        } while (p != outerNode);
        if (!m) return nullptr;
        const EarNode *stop = m;
        const double mx = m->x, my = m->y;
        double tanMin = std::numeric_limits<double>::infinity();
        p = m;
        do {
            if (hx >= p->x && p->x >= mx && hx != p->x
                && pointInTriangle(hy < my ? hx : qx, hy, mx, my, hy < my ? qx : hx, hy, p->x, p->y)) {
                const double tan = std::fabs(hy - p->y) / (hx - p->x);
                if (locallyInside(p, hole)
                    && (tan < tanMin || (tan == tanMin && (p->x > m->x || (p->x == m->x && sectorContainsSector(m, p)))))) {
                    m = p;
                    tanMin = tan;
                }
            }
            p = p->next;
        } while (p != stop);
        return m;
    }

    EarNode *eliminateHole(EarNode *hole, EarNode *outerNode) {
        EarNode *bridge = findHoleBridge(hole, outerNode);
        if (!bridge) return outerNode;
        EarNode *bridgeReverse = splitPolygon(bridge, hole);
        filterPoints(bridgeReverse, bridgeReverse->next);
        return filterPoints(bridge, bridge->next);
    }
};

}

std::vector<std::array<int, 3>> triangulatePolygon(const std::vector<Vec2> &outer, const std::vector<std::vector<Vec2>> &holes) {
    EarCut earcut;
    earcut.run(outer, holes);
    return std::move(earcut.triangles);
}

// --- Campionamento delle curve --------------------------------------------------

namespace {

double distanceToSegment(const Vec3 &p, const Vec3 &a, const Vec3 &b) {
    const Vec3 ab = b - a;
    const double length2 = squaredNorm(ab);
    if (length2 <= 0.0) return distance(p, a);
    const double s = std::clamp(dot(p - a, ab) / length2, 0.0, 1.0);
    return distance(p, a + s * ab);
}

double angleBetween(const Vec3 &a, const Vec3 &b) {
    const double na = norm(a), nb = norm(b);
    if (na <= 0.0 || nb <= 0.0) return 0.0;
    return std::atan2(norm(cross(a, b)), dot(a, b));
}

// `knots`: nodi della curva dentro l'intervallo. Un intervallo con molti nodi
// (le curve d'intersezione tracciate ne hanno centinaia) si divide comunque:
// il controllo su tre punti vale per un tratto quasi polinomiale.
void subdivideCurve(const Curve<3> &curve, double a, double b, const Vec3 &pa, const Vec3 &pb, const TessellationOptions &options,
                    const std::vector<double> &knots, int depth, std::vector<double> &out) {
    const auto inside = std::upper_bound(knots.begin(), knots.end(), b) - std::lower_bound(knots.begin(), knots.end(), a);
    bool split = (depth < 1 && distance(pa, pb) <= options.deflection) || inside > 10;  // curva chiusa: almeno due tratti
    if (!split && depth < 20) {
        for (double s : {0.25, 0.5, 0.75})
            if (distanceToSegment(curve.point(a + s * (b - a)), pa, pb) > options.deflection) {
                split = true;
                break;
            }
        if (!split) {
            const double h = 1e-9 * (b - a);
            Vec3 da[2], db[2];
            curve.evaluate(a + h, 1, da);
            curve.evaluate(b - h, 1, db);
            split = angleBetween(da[1], db[1]) > options.angle;
        }
    }
    if (split && depth < 20) {
        const double m = 0.5 * (a + b);
        const Vec3 pm = curve.point(m);
        subdivideCurve(curve, a, m, pa, pm, options, knots, depth + 1, out);
        subdivideCurve(curve, m, b, pm, pb, options, knots, depth + 1, out);
        return;
    }
    out.push_back(b);
}

}

std::vector<double> sampleCurve(const Curve<3> &curve, const Interval &range, const TessellationOptions &options) {
    std::vector<double> breaks = curve.breakpoints(range);
    if (breaks.size() < 2) breaks = {range.lo, range.hi};
    // Si parte solo dai nodi in cui la tangente gira (spigoli della curva).
    std::vector<double> hard{breaks.front()};
    for (std::size_t i = 1; i + 1 < breaks.size(); ++i) {
        Vec3 left[2], right[2];
        curve.evaluateLeft(breaks[i], 1, left);
        curve.evaluate(breaks[i], 1, right);
        if (angleBetween(left[1], right[1]) > 1e-3) hard.push_back(breaks[i]);
    }
    hard.push_back(breaks.back());
    std::vector<double> ts{hard.front()};
    for (std::size_t i = 0; i + 1 < hard.size(); ++i) {
        if (hard[i + 1] <= hard[i]) continue;
        subdivideCurve(curve, hard[i], hard[i + 1], curve.point(hard[i]), curve.point(hard[i + 1]), options, breaks, 0, ts);
    }
    return ts;
}

// --- Triangolazione delle facce -------------------------------------------------

namespace {

// Punto del bordo di un poligono nello spazio (u, v) della finestra.
struct BoundaryPoint {
    Vec2 uv;
    Vec3 p;
};

// Campione di un loop: il tratto verso il campione successivo appartiene alla
// fin `fin` (SP-curve traslata di `shift`) tra i parametri t e tNext.
struct LoopSample {
    Vec2 uv;
    Vec3 p;
    int fin;
    double t, tNext;
    Vec2 shift;
};

struct LoopRing {
    std::vector<LoopSample> samples;
    Vec2 wrap;  // uv alla chiusura meno uv del primo campione (periodi interi)
};

enum Side { Bottom, Right, Top, Left, Inner };

// Pezzo di loop dentro una cella della finestra, con gli estremi sul bordo della cella.
struct Chain {
    std::vector<BoundaryPoint> points;
    Side start = Inner, end = Inner;
    int cell = 0;
};

// Loop tutto dentro una cella.
struct ClosedRing {
    std::vector<BoundaryPoint> points;
    int cell = 0;
};

struct Cell {
    Vec2 lo, hi;
    Vec2 center() const { return 0.5 * (lo + hi); }
};

class FaceTessellator {
public:
    FaceTessellator(const Body &body, FaceId face, const std::vector<std::vector<double>> &edgeSamples,
                    const TessellationOptions &options)
        : body_(body), faceId_(face), face_(body.face(face)), surface_(*face_.surface), edgeSamples_(edgeSamples),
          options_(options) {
        periodic_[0] = surface_.isUPeriodic();
        periodic_[1] = surface_.isVPeriodic();
        period_[0] = surface_.uPeriod();
        period_[1] = surface_.vPeriod();
    }

    FaceMesh run();

private:
    const Body &body_;
    FaceId faceId_;
    const Face &face_;
    const Surface &surface_;
    const std::vector<std::vector<double>> &edgeSamples_;
    TessellationOptions options_;
    bool periodic_[2];
    double period_[2];
    double lo_[2] = {0.0, 0.0}, hi_[2] = {0.0, 0.0};  // finestra
    // Bordi delle celle in cui si divide la finestra: i suoi estremi e gli
    // spigoli vivi della superficie (nodi in cui la normale salta), che cosi'
    // diventano lati dei triangoli.
    std::vector<double> cuts_[2];
    double scale_[2] = {1.0, 1.0};  // |Su|, |Sv| tipici

    std::vector<const Fin *> fins_;
    std::vector<LoopRing> rings_;
    std::vector<Chain> chains_;
    std::vector<ClosedRing> closed_;

    // Mesh della faccia.
    std::vector<Vec2> uv_;
    std::vector<Vec3> points_, normals_;
    std::vector<std::array<int, 3>> triangles_;
    std::map<std::pair<int, int>, int> fixed_;  // lati del bordo (non si dividono ne' si scambiano)
    std::map<std::pair<int, int>, std::array<int, 2>> adjacency_;  // triangoli ai due lati di ogni lato

    int strips(int d) const { return int(cuts_[d].size()) - 1; }
    int cellCount() const { return strips(0) * strips(1); }
    Cell cell(int index) const {
        const int i = index / strips(1), j = index % strips(1);
        return {Vec2(cuts_[0][std::size_t(i)], cuts_[1][std::size_t(j)]), Vec2(cuts_[0][std::size_t(i) + 1], cuts_[1][std::size_t(j) + 1])};
    }
    void buildRings();
    void chooseWindow();
    std::vector<double> creases(int dimension) const;
    void cutRing(const LoopRing &ring);
    double crossingParameter(const LoopSample &sample, int dimension, double line) const;
    std::vector<std::vector<BoundaryPoint>> walkChains(const std::vector<std::size_t> &chains, const Cell &box);
    void appendPath(std::vector<BoundaryPoint> &polygon, const Vec2 &from, const Vec2 &to, const Vec2 &center);
    void subdividePath(const Vec2 &a, const Vec2 &b, const Vec3 &pa, const Vec3 &pb, const Vec2 &center, int depth,
                       std::vector<BoundaryPoint> &out);
    std::vector<BoundaryPoint> rectangle(const Cell &box);
    void triangulate(const std::vector<std::vector<BoundaryPoint>> &polygons, const Vec2 &center);
    int addVertex(const Vec2 &uv, const Vec3 &p, const Vec2 &center);
    Vec3 surfaceNormal(const Vec2 &uv, const Vec2 &center) const;
    bool needsSplit(int a, int b) const;
    static std::pair<int, int> key(int a, int b) { return {std::min(a, b), std::max(a, b)}; }
    void link(int t);
    void unlink(int t);
    void legalize(std::vector<std::pair<int, int>> &pending);
    void refine();
};

// Normale in un punto della cella: calcolata appena dentro la cella (sugli
// spigoli vivi vale quella del lato giusto) e, nei punti singolari (poli,
// vertice del cono), un po' piu' dentro.
Vec3 FaceTessellator::surfaceNormal(const Vec2 &uv, const Vec2 &center) const {
    for (double step : {1e-9, 1e-7, 1e-5, 1e-3, 1e-2}) {
        try {
            const Vec2 q = uv + (center - uv) * step;
            return surface_.normal(q[0], q[1]);
        } catch (const std::exception &) {
        }
    }
    return Vec3(0, 0, 1);
}

void FaceTessellator::buildRings() {
    for (LoopId l : face_.loops) {
        const std::vector<FinId> fins = body_.loopFins(l);
        if (fins.empty()) continue;
        LoopRing ring;
        Vec2 previous;
        bool first = true;
        Vec2 firstUV;
        for (FinId f : fins) {
            const Fin &fin = body_.fin(f);
            const Edge &edge = body_.edge(fin.edge);
            if (!fin.pcurve) throw std::logic_error("tessellate: fin senza SP-curve");
            const int finIndex = int(fins_.size());
            fins_.push_back(&fin);
            std::vector<double> ts = edgeSamples_.at(std::size_t(fin.edge.index));
            if (!fin.sense) std::reverse(ts.begin(), ts.end());
            Vec2 shift;
            const Vec2 start = fin.pcurve->point(ts.front());
            if (first) {
                firstUV = start;
                first = false;
            } else {
                for (int d = 0; d < 2; ++d)
                    if (periodic_[d]) shift[d] = period_[d] * std::round((previous[d] - start[d]) / period_[d]);
            }
            for (std::size_t j = 0; j + 1 < ts.size(); ++j) {
                LoopSample sample;
                sample.uv = fin.pcurve->point(ts[j]) + shift;
                sample.p = edge.curve->point(ts[j]);
                sample.fin = finIndex;
                sample.t = ts[j];
                sample.tNext = ts[j + 1];
                sample.shift = shift;
                ring.samples.push_back(sample);
            }
            previous = fin.pcurve->point(ts.back()) + shift;
        }
        if (ring.samples.empty()) continue;
        ring.wrap = previous - firstUV;
        for (int d = 0; d < 2; ++d) ring.wrap[d] = periodic_[d] ? period_[d] * std::round(ring.wrap[d] / period_[d]) : 0.0;
        rings_.push_back(std::move(ring));
    }
}

// Nodi della superficie nella finestra in cui la normale e' discontinua.
std::vector<double> FaceTessellator::creases(int d) const {
    const int o = 1 - d;
    std::vector<double> result;
    const double width = hi_[d] - lo_[d];
    std::vector<double> breaks;
    if (periodic_[d]) {
        const Interval domain = d == 0 ? surface_.uDomain() : surface_.vDomain();
        const Interval base{domain.lo, domain.lo + period_[d]};
        breaks = d == 0 ? surface_.uBreakpoints(base) : surface_.vBreakpoints(base);
        for (double &b : breaks) b -= period_[d] * std::floor((b - lo_[d]) / period_[d]);
    } else {
        const Interval range{lo_[d], hi_[d]};
        breaks = d == 0 ? surface_.uBreakpoints(range) : surface_.vBreakpoints(range);
    }
    const double h = 1e-7 * width;
    for (double b : breaks) {
        if (b <= lo_[d] + 1e-9 * width || b >= hi_[d] - 1e-9 * width) continue;
        bool crease = false;
        for (double s : {0.2, 0.5, 0.8}) {
            const double other = lo_[o] + s * (hi_[o] - lo_[o]);
            try {
                Vec2 left, right;
                left[d] = b - h;
                right[d] = b + h;
                left[o] = right[o] = other;
                const Vec3 nl = surface_.normal(left[0], left[1]), nr = surface_.normal(right[0], right[1]);
                crease = crease || angleBetween(nl, nr) > 1e-3;
            } catch (const std::exception &) {
            }
        }
        if (crease) result.push_back(b);
    }
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

// Finestra di un periodo nelle direzioni periodiche, con il taglio nel
// piu' grande intervallo libero dai campioni (il taglio non passa per un campione).
void FaceTessellator::chooseWindow() {
    for (int d = 0; d < 2; ++d) {
        const Interval domain = d == 0 ? surface_.uDomain() : surface_.vDomain();
        std::vector<double> values;
        for (const LoopRing &ring : rings_)
            for (const LoopSample &sample : ring.samples) values.push_back(sample.uv[d]);
        if (periodic_[d]) {
            const double p = period_[d];
            if (values.empty()) {
                lo_[d] = domain.lo;
            } else {
                for (double &value : values) value -= p * std::floor(value / p);
                std::sort(values.begin(), values.end());
                double bestGap = values.front() + p - values.back(), cut = values.back() + 0.5 * bestGap;
                for (std::size_t i = 0; i + 1 < values.size(); ++i)
                    if (values[i + 1] - values[i] > bestGap) {
                        bestGap = values[i + 1] - values[i];
                        cut = 0.5 * (values[i] + values[i + 1]);
                    }
                lo_[d] = cut - p * std::floor(cut / p);
            }
            hi_[d] = lo_[d] + p;
        } else if (domain.isFinite()) {
            lo_[d] = domain.lo;
            hi_[d] = domain.hi;
        } else if (!values.empty()) {
            // Il vertice del cono puo' essere dentro la faccia: la finestra lo comprende.
            if (d == 1 && surface_.type() == SurfaceType::Cone) {
                const auto &cone = static_cast<const ConicalSurface &>(surface_);
                values.push_back(-cone.referenceRadius() / std::sin(cone.semiAngle()));
            }
            lo_[d] = *std::min_element(values.begin(), values.end());
            hi_[d] = *std::max_element(values.begin(), values.end());
        } else {
            throw std::domain_error("tessellate: faccia illimitata");
        }
    }
    for (int d = 0; d < 2; ++d) {
        cuts_[d] = {lo_[d]};
        for (double c : creases(d)) cuts_[d].push_back(c);
        cuts_[d].push_back(hi_[d]);
    }
    // Scala tipica di u e v per triangolare in un piano quasi isometrico.
    Vec3 d[4];
    surface_.evaluate(0.5 * (lo_[0] + hi_[0]), 0.5 * (lo_[1] + hi_[1]), 1, d);
    const double su = norm(d[Surface::derivativeIndex(1, 0, 1)]), sv = norm(d[Surface::derivativeIndex(0, 1, 1)]);
    scale_[0] = su > 1e-12 ? su : 1.0;
    scale_[1] = sv > 1e-12 ? sv : 1.0;
}

double FaceTessellator::crossingParameter(const LoopSample &sample, int dimension, double line) const {
    const Curve<2> &pcurve = *fins_[std::size_t(sample.fin)]->pcurve;
    double a = sample.t, b = sample.tNext;
    double fa = pcurve.point(a)[dimension] + sample.shift[dimension] - line;
    for (int i = 0; i < 60; ++i) {
        const double m = 0.5 * (a + b);
        const double fm = pcurve.point(m)[dimension] + sample.shift[dimension] - line;
        if ((fm < 0.0) == (fa < 0.0)) {
            a = m;
            fa = fm;
        } else {
            b = m;
        }
    }
    return 0.5 * (a + b);
}

// Spezza il loop dove attraversa i bordi delle celle: catene con gli estremi
// sul bordo di una cella, o un poligono chiuso se resta in una cella.
void FaceTessellator::cutRing(const LoopRing &ring) {
    const std::size_t n = ring.samples.size();
    int strip[2];
    double period[2];  // periodi interi tra le coordinate srotolate e quelle della finestra
    for (int d = 0; d < 2; ++d) {
        const double x = ring.samples[0].uv[d];
        period[d] = periodic_[d] ? std::floor((x - lo_[d]) / period_[d]) : 0.0;
        const double r = x - period[d] * period_[d];
        strip[d] = int(std::upper_bound(cuts_[d].begin(), cuts_[d].end(), r) - cuts_[d].begin()) - 1;
        strip[d] = std::clamp(strip[d], 0, strips(d) - 1);
    }
    auto local = [&](const Vec2 &uv) { return Vec2(uv[0] - period[0] * period_[0], uv[1] - period[1] * period_[1]); };
    auto cellIndex = [&] { return strip[0] * strips(1) + strip[1]; };

    std::vector<Chain> pieces;
    Chain current;
    for (std::size_t i = 0; i < n; ++i) {
        const LoopSample &sample = ring.samples[i];
        if (current.points.empty() || distance(current.points.back().p, sample.p) != 0.0)
            current.points.push_back({local(sample.uv), sample.p});
        const Vec2 a = sample.uv, b = i + 1 < n ? ring.samples[i + 1].uv : ring.samples[0].uv + ring.wrap;
        // Attraversamenti del tratto, in ordine lungo il tratto.
        struct Crossing {
            double fraction;
            int dimension;
            double line;
            bool positive;
        };
        std::vector<Crossing> crossings;
        for (int d = 0; d < 2; ++d) {
            const double lower = std::min(a[d], b[d]), upper = std::max(a[d], b[d]);
            if (upper <= lower) continue;
            // Un campione sul bordo di una cella (i nodi della superficie sono
            // anche nodi delle curve) appartiene alla cella sopra: intervalli
            // semiaperti, come nell'assegnazione delle celle.
            const bool positive = b[d] > a[d];
            auto consider = [&](double line) {
                if (positive ? (line > a[d] && line <= b[d]) : (line > b[d] && line <= a[d]))
                    crossings.push_back({(line - a[d]) / (b[d] - a[d]), d, line, positive});
            };
            if (periodic_[d]) {
                for (int c = 0; c < strips(d); ++c) {
                    const double base = cuts_[d][std::size_t(c)];
                    for (double k = std::ceil((lower - base) / period_[d]); base + k * period_[d] <= upper; k += 1.0)
                        consider(base + k * period_[d]);
                }
            } else {
                for (int c = 1; c < strips(d); ++c) consider(cuts_[d][std::size_t(c)]);
            }
        }
        std::sort(crossings.begin(), crossings.end(), [](const Crossing &x, const Crossing &y) { return x.fraction < y.fraction; });
        for (const Crossing &crossing : crossings) {
            const int d = crossing.dimension;
            const double t = crossing.fraction <= 0.0   ? sample.t
                           : crossing.fraction >= 1.0 ? sample.tNext
                                                      : crossingParameter(sample, d, crossing.line);
            const Curve<2> &pcurve = *fins_[std::size_t(sample.fin)]->pcurve;
            const Vec2 uv = pcurve.point(t) + sample.shift;
            const Vec3 p = body_.edge(fins_[std::size_t(sample.fin)]->edge).curve->point(t);
            // Fine della catena sul bordo della cella corrente.
            Vec2 end = local(uv);
            end[d] = cuts_[d][std::size_t(strip[d] + (crossing.positive ? 1 : 0))];
            if (!current.points.empty() && distance(current.points.back().p, p) == 0.0) current.points.pop_back();
            current.points.push_back({end, p});
            current.end = d == 0 ? (crossing.positive ? Right : Left) : (crossing.positive ? Top : Bottom);
            current.cell = cellIndex();
            pieces.push_back(std::move(current));
            current = Chain();
            if (crossing.positive) {
                if (++strip[d] == strips(d)) {
                    strip[d] = 0;
                    period[d] += 1.0;
                }
            } else if (--strip[d] < 0) {
                strip[d] = strips(d) - 1;
                period[d] -= 1.0;
            }
            Vec2 start = local(uv);
            start[d] = cuts_[d][std::size_t(strip[d] + (crossing.positive ? 0 : 1))];
            current.points.push_back({start, p});
            current.start = d == 0 ? (crossing.positive ? Left : Right) : (crossing.positive ? Bottom : Top);
        }
    }
    if (pieces.empty()) {
        closed_.push_back({std::move(current.points), cellIndex()});
        return;
    }
    // Il tratto prima del primo attraversamento chiude l'ultima catena.
    Chain &head = pieces.front();
    std::size_t skip = !current.points.empty() && distance(current.points.back().p, head.points.front().p) == 0.0 ? 1 : 0;
    current.points.insert(current.points.end(), head.points.begin() + std::ptrdiff_t(skip), head.points.end());
    current.end = head.end;
    current.cell = head.cell;
    head = std::move(current);
    for (Chain &chain : pieces) chains_.push_back(std::move(chain));
}

void FaceTessellator::subdividePath(const Vec2 &a, const Vec2 &b, const Vec3 &pa, const Vec3 &pb, const Vec2 &center, int depth,
                                    std::vector<BoundaryPoint> &out) {
    const Vec2 m = 0.5 * (a + b);
    const Vec3 pm = surface_.point(m[0], m[1]);
    // Anche i lati degeneri (poli, vertice del cono) si dividono se la normale
    // cambia lungo il lato: i triangoli che vi arrivano hanno cosi' ciascuno
    // il suo vertice, con il suo u.
    bool split = depth < 14 && (distanceToSegment(pm, pa, pb) > options_.deflection
                                || angleBetween(surfaceNormal(a, center), surfaceNormal(m, center)) > 0.5 * options_.angle
                                || angleBetween(surfaceNormal(m, center), surfaceNormal(b, center)) > 0.5 * options_.angle);
    if (!split && depth < 14) {
        for (double s : {0.25, 0.75}) {
            const Vec2 q = a + s * (b - a);
            if (distanceToSegment(surface_.point(q[0], q[1]), pa, pb) > options_.deflection) split = true;
        }
    }
    if (!split) return;
    subdividePath(a, m, pa, pm, center, depth + 1, out);
    out.push_back({m, pm});
    subdividePath(m, b, pm, pb, center, depth + 1, out);
}

// Tratto rettilineo nello spazio (u, v) lungo il bordo di una cella (esclusi
// gli estremi), infittito come gli edge. Lo stesso lato visto da due celle
// (o dai due lati di un taglio periodico) si campiona nello stesso modo
// ordinando gli estremi.
void FaceTessellator::appendPath(std::vector<BoundaryPoint> &polygon, const Vec2 &from, const Vec2 &to, const Vec2 &center) {
    const bool swap = from[0] > to[0] || (from[0] == to[0] && from[1] > to[1]);
    const Vec2 a = swap ? to : from, b = swap ? from : to;
    std::vector<BoundaryPoint> points;
    subdividePath(a, b, surface_.point(a[0], a[1]), surface_.point(b[0], b[1]), center, 0, points);
    if (swap) std::reverse(points.begin(), points.end());
    polygon.insert(polygon.end(), points.begin(), points.end());
}

// Ricompone i poligoni di una cella: dalla fine di ogni catena si prosegue
// lungo il bordo della cella in senso antiorario fino al prossimo inizio.
std::vector<std::vector<BoundaryPoint>> FaceTessellator::walkChains(const std::vector<std::size_t> &chains, const Cell &box) {
    std::vector<std::vector<BoundaryPoint>> polygons;
    const double w = box.hi[0] - box.lo[0], h = box.hi[1] - box.lo[1], total = 2.0 * (w + h);
    const Vec2 corners[4] = {Vec2(box.hi[0], box.lo[1]), box.hi, Vec2(box.lo[0], box.hi[1]), box.lo};
    const double cornerPositions[4] = {w, w + h, 2.0 * w + h, total};
    auto perimeter = [&](const Vec2 &uv, Side side) {
        switch (side) {
        case Bottom: return uv[0] - box.lo[0];
        case Right: return w + uv[1] - box.lo[1];
        case Top: return w + h + box.hi[0] - uv[0];
        case Left: return 2.0 * w + h + box.hi[1] - uv[1];
        default: return 0.0;
        }
    };
    std::vector<bool> used(chains.size(), false);
    for (std::size_t first = 0; first < chains.size(); ++first) {
        if (used[first]) continue;
        std::vector<BoundaryPoint> polygon;
        std::size_t current = first;
        used[first] = true;
        for (std::size_t guard = 0; guard <= chains.size(); ++guard) {
            const Chain &chain = chains_[chains[current]];
            polygon.insert(polygon.end(), chain.points.begin(), chain.points.end());
            const double sEnd = perimeter(chain.points.back().uv, chain.end);
            // Prossimo inizio di catena lungo il bordo in senso antiorario.
            std::size_t next = chains.size();
            double best = 1e300;
            for (std::size_t k = 0; k < chains.size(); ++k) {
                if (used[k] && k != first) continue;
                const Chain &candidate = chains_[chains[k]];
                double gap = perimeter(candidate.points.front().uv, candidate.start) - sEnd;
                if (gap <= 0.0) gap += total;
                if (gap < best) {
                    best = gap;
                    next = k;
                }
            }
            if (next == chains.size()) throw std::domain_error("tessellate: bordo della cella non ricomposto");
            // Angoli della cella tra la fine e il prossimo inizio.
            Vec2 from = chain.points.back().uv;
            for (int turn = 0; turn < 8; ++turn) {
                const int c = turn % 4;
                // Un angolo che coincide (a meno dell'arrotondamento) con un estremo non si aggiunge.
                const double gap = cornerPositions[c] - sEnd + (turn >= 4 ? total : 0.0);
                if (gap <= 1e-9 * total || gap >= best - 1e-9 * total) continue;
                appendPath(polygon, from, corners[c], box.center());
                polygon.push_back({corners[c], surface_.point(corners[c][0], corners[c][1])});
                from = corners[c];
            }
            appendPath(polygon, from, chains_[chains[next]].points.front().uv, box.center());
            if (next == first) break;
            used[next] = true;
            current = next;
        }
        polygons.push_back(std::move(polygon));
    }
    return polygons;
}

std::vector<BoundaryPoint> FaceTessellator::rectangle(const Cell &box) {
    const Vec2 corners[4] = {box.lo, Vec2(box.hi[0], box.lo[1]), box.hi, Vec2(box.lo[0], box.hi[1])};
    std::vector<BoundaryPoint> polygon;
    for (int c = 0; c < 4; ++c) {
        polygon.push_back({corners[c], surface_.point(corners[c][0], corners[c][1])});
        appendPath(polygon, corners[c], corners[(c + 1) % 4], box.center());
    }
    return polygon;
}

double signedArea(const std::vector<BoundaryPoint> &polygon) {
    double area = 0.0;
    for (std::size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++) area += cross(polygon[j].uv, polygon[i].uv);
    return 0.5 * area;
}

bool insidePolygon(const std::vector<BoundaryPoint> &polygon, const Vec2 &q) {
    bool inside = false;
    for (std::size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++) {
        const Vec2 &a = polygon[i].uv, &b = polygon[j].uv;
        if ((a[1] > q[1]) != (b[1] > q[1]) && q[0] < (b[0] - a[0]) * (q[1] - a[1]) / (b[1] - a[1]) + a[0]) inside = !inside;
    }
    return inside;
}

int FaceTessellator::addVertex(const Vec2 &uv, const Vec3 &p, const Vec2 &center) {
    uv_.push_back(uv);
    points_.push_back(p);
    normals_.push_back(surfaceNormal(uv, center));
    return int(uv_.size()) - 1;
}

// Poligoni di una cella: esterni antiorari, fori orari (assegnati
// all'esterno piu' piccolo che li contiene).
void FaceTessellator::triangulate(const std::vector<std::vector<BoundaryPoint>> &polygons, const Vec2 &center) {
    std::vector<std::size_t> outers, holes;
    std::vector<double> areas;
    for (std::size_t i = 0; i < polygons.size(); ++i) {
        areas.push_back(polygons[i].size() >= 3 ? signedArea(polygons[i]) : 0.0);
        if (areas[i] > 0.0) outers.push_back(i);
        else if (areas[i] < 0.0) holes.push_back(i);
    }
    std::vector<std::vector<std::size_t>> holesOf(polygons.size());
    for (std::size_t hole : holes) {
        std::size_t owner = polygons.size();
        for (std::size_t outer : outers)
            if (insidePolygon(polygons[outer], polygons[hole].front().uv) && (owner == polygons.size() || areas[outer] < areas[owner]))
                owner = outer;
        if (owner == polygons.size() && !outers.empty())
            owner = *std::max_element(outers.begin(), outers.end(), [&](std::size_t a, std::size_t b) { return areas[a] < areas[b]; });
        if (owner != polygons.size()) holesOf[owner].push_back(hole);
    }
    auto scaled = [&](const Vec2 &uv) { return Vec2(uv[0] * scale_[0], uv[1] * scale_[1]); };
    for (std::size_t outer : outers) {
        std::vector<int> ids;
        std::vector<Vec2> outerRing;
        std::vector<std::vector<Vec2>> holeRings;
        auto addRing = [&](const std::vector<BoundaryPoint> &polygon, std::vector<Vec2> &ring) {
            const int first = int(uv_.size());
            for (std::size_t i = 0; i < polygon.size(); ++i) {
                const int id = addVertex(polygon[i].uv, polygon[i].p, center);
                ids.push_back(id);
                ring.push_back(scaled(polygon[i].uv));
                const int next = i + 1 < polygon.size() ? id + 1 : first;
                fixed_[key(id, next)] = 1;
            }
        };
        addRing(polygons[outer], outerRing);
        for (std::size_t hole : holesOf[outer]) {
            holeRings.emplace_back();
            addRing(polygons[hole], holeRings.back());
        }
        for (const std::array<int, 3> &t : triangulatePolygon(outerRing, holeRings))
            triangles_.push_back({ids[std::size_t(t[0])], ids[std::size_t(t[1])], ids[std::size_t(t[2])]});
    }
}

bool FaceTessellator::needsSplit(int a, int b) const {
    const Vec3 &pa = points_[std::size_t(a)], &pb = points_[std::size_t(b)];
    if (distance(pa, pb) <= 0.5 * options_.deflection) return false;
    const Vec2 m = 0.5 * (uv_[std::size_t(a)] + uv_[std::size_t(b)]);
    if (distanceToSegment(surface_.point(m[0], m[1]), pa, pb) > options_.deflection) return true;
    return angleBetween(normals_[std::size_t(a)], normals_[std::size_t(b)]) > options_.angle;
}

void FaceTessellator::link(int t) {
    for (int k = 0; k < 3; ++k) {
        const int a = triangles_[std::size_t(t)][std::size_t(k)], b = triangles_[std::size_t(t)][std::size_t((k + 1) % 3)];
        std::array<int, 2> &sides = adjacency_.try_emplace(key(a, b), std::array<int, 2>{-1, -1}).first->second;
        (sides[0] < 0 ? sides[0] : sides[1]) = t;
    }
}

void FaceTessellator::unlink(int t) {
    for (int k = 0; k < 3; ++k) {
        const int a = triangles_[std::size_t(t)][std::size_t(k)], b = triangles_[std::size_t(t)][std::size_t((k + 1) % 3)];
        auto found = adjacency_.find(key(a, b));
        if (found == adjacency_.end()) continue;
        std::array<int, 2> &sides = found->second;
        if (sides[0] == t) sides[0] = sides[1];
        else if (sides[1] != t) continue;
        sides[1] = -1;
        if (sides[0] < 0) adjacency_.erase(found);
    }
}

// Scambi di diagonale verso la triangolazione di Delaunay (nel piano (u, v)
// scalato), a partire dai lati in `pending`, senza toccare i lati del bordo.
void FaceTessellator::legalize(std::vector<std::pair<int, int>> &pending) {
    auto at = [&](int i) { return Vec2(uv_[std::size_t(i)][0] * scale_[0], uv_[std::size_t(i)][1] * scale_[1]); };
    auto orient = [](const Vec2 &a, const Vec2 &b, const Vec2 &c) { return cross(b - a, c - a); };
    auto inCircle = [](const Vec2 &a, const Vec2 &b, const Vec2 &c, const Vec2 &d) {
        const Vec2 ad = a - d, bd = b - d, cd = c - d;
        return squaredNorm(ad) * cross(bd, cd) + squaredNorm(bd) * cross(cd, ad) + squaredNorm(cd) * cross(ad, bd);
    };
    std::size_t budget = 50 * (triangles_.size() + 16);
    while (!pending.empty() && budget-- > 0) {
        const std::pair<int, int> edge = pending.back();
        pending.pop_back();
        if (fixed_.count(edge)) continue;
        auto found = adjacency_.find(edge);
        if (found == adjacency_.end() || found->second[1] < 0) continue;
        const int t1 = found->second[0], t2 = found->second[1];
        auto corner = [&](int t, int &p, int &q, int &c) {
            const std::array<int, 3> &tri = triangles_[std::size_t(t)];
            for (int k = 0; k < 3; ++k)
                if (key(tri[std::size_t(k)], tri[std::size_t((k + 1) % 3)]) == edge) {
                    p = tri[std::size_t(k)];
                    q = tri[std::size_t((k + 1) % 3)];
                    c = tri[std::size_t((k + 2) % 3)];
                }
        };
        int p = -1, q = -1, c = -1, q2 = -1, p2 = -1, d = -1;
        corner(t1, p, q, c);
        corner(t2, q2, p2, d);
        if (c < 0 || d < 0 || c == d || p != p2 || q != q2) continue;
        const Vec2 P = at(p), Q = at(q), C = at(c), D = at(d);
        const double size = squaredNorm(Q - P) + squaredNorm(C - D);
        if (inCircle(P, Q, C, D) <= 1e-12 * size * size) continue;
        // Nuovi triangoli (p, d, c) e (d, q, c): devono restare antiorari.
        if (orient(P, D, C) <= 1e-12 * size || orient(D, Q, C) <= 1e-12 * size) continue;
        if (adjacency_.count(key(c, d))) continue;
        unlink(t1);
        unlink(t2);
        triangles_[std::size_t(t1)] = {p, d, c};
        triangles_[std::size_t(t2)] = {d, q, c};
        link(t1);
        link(t2);
        for (const std::pair<int, int> &outer : {key(p, d), key(d, q), key(q, c), key(c, p)}) pending.push_back(outer);
    }
}

// Divide i lati interni troppo lontani dalla superficie o con normali troppo
// diverse agli estremi (prima i piu' lunghi); i triangoli con i tre lati sul
// bordo si dividono nel baricentro. Dopo ogni giro, scambi di Delaunay.
void FaceTessellator::refine() {
    constexpr std::size_t kMaxVertices = 200000;
    adjacency_.clear();
    for (std::size_t t = 0; t < triangles_.size(); ++t) link(int(t));
    std::vector<std::pair<int, int>> pending;
    for (const auto &entry : adjacency_) pending.push_back(entry.first);
    legalize(pending);
    for (int round = 0; round < 60 && uv_.size() < kMaxVertices; ++round) {
        struct Candidate {
            double length;
            std::pair<int, int> edge;
        };
        std::vector<Candidate> candidates;
        for (const auto &entry : adjacency_) {
            if (entry.second[1] < 0 || fixed_.count(entry.first)) continue;
            if (needsSplit(entry.first.first, entry.first.second))
                candidates.push_back({distance(points_[std::size_t(entry.first.first)], points_[std::size_t(entry.first.second)]), entry.first});
        }
        std::vector<int> centroids;
        for (std::size_t t = 0; t < triangles_.size(); ++t) {
            const std::array<int, 3> &tri = triangles_[t];
            if (!fixed_.count(key(tri[0], tri[1])) || !fixed_.count(key(tri[1], tri[2])) || !fixed_.count(key(tri[2], tri[0]))) continue;
            const Vec2 c = (uv_[std::size_t(tri[0])] + uv_[std::size_t(tri[1])] + uv_[std::size_t(tri[2])]) / 3.0;
            // Distanza dal piano del triangolo.
            const Vec3 &a = points_[std::size_t(tri[0])], &b = points_[std::size_t(tri[1])], &d = points_[std::size_t(tri[2])];
            const Vec3 n = cross(b - a, d - a);
            const double length = norm(n);
            if (length > 0.0 && std::fabs(dot(surface_.point(c[0], c[1]) - a, n)) / length > options_.deflection) centroids.push_back(int(t));
        }
        if (candidates.empty() && centroids.empty()) break;
        std::sort(candidates.begin(), candidates.end(), [](const Candidate &a, const Candidate &b) { return a.length > b.length; });
        std::vector<bool> touched(triangles_.size(), false);
        for (int t : centroids) {
            const std::array<int, 3> tri = triangles_[std::size_t(t)];
            const Vec2 c = (uv_[std::size_t(tri[0])] + uv_[std::size_t(tri[1])] + uv_[std::size_t(tri[2])]) / 3.0;
            const int m = addVertex(c, surface_.point(c[0], c[1]), c);
            unlink(t);
            triangles_[std::size_t(t)] = {tri[0], tri[1], m};
            triangles_.push_back({tri[1], tri[2], m});
            triangles_.push_back({tri[2], tri[0], m});
            link(t);
            link(int(triangles_.size()) - 2);
            link(int(triangles_.size()) - 1);
            touched[std::size_t(t)] = true;
            for (int k = 0; k < 3; ++k) pending.push_back(key(tri[std::size_t(k)], tri[std::size_t((k + 1) % 3)]));
        }
        for (const Candidate &candidate : candidates) {
            auto found = adjacency_.find(candidate.edge);
            if (found == adjacency_.end() || found->second[1] < 0) continue;
            const int t1 = found->second[0], t2 = found->second[1];
            if (touched.size() < triangles_.size()) touched.resize(triangles_.size(), true);  // nuovi in questo giro
            if (touched[std::size_t(t1)] || touched[std::size_t(t2)]) continue;
            touched[std::size_t(t1)] = touched[std::size_t(t2)] = true;
            const int a = candidate.edge.first, b = candidate.edge.second;
            const Vec2 uv = 0.5 * (uv_[std::size_t(a)] + uv_[std::size_t(b)]);
            const int m = addVertex(uv, surface_.point(uv[0], uv[1]), uv);
            for (int t : {t1, t2}) {
                const std::array<int, 3> tri = triangles_[std::size_t(t)];
                int k = 0;
                while (key(tri[std::size_t(k)], tri[std::size_t((k + 1) % 3)]) != candidate.edge) ++k;
                const int p = tri[std::size_t(k)], q = tri[std::size_t((k + 1) % 3)], r = tri[std::size_t((k + 2) % 3)];
                unlink(t);
                triangles_[std::size_t(t)] = {p, m, r};
                triangles_.push_back({m, q, r});
                link(t);
                link(int(triangles_.size()) - 1);
                pending.push_back(key(q, r));
                pending.push_back(key(r, p));
                pending.push_back(key(m, r));
            }
        }
        legalize(pending);
    }
}

FaceMesh FaceTessellator::run() {
    buildRings();
    chooseWindow();
    for (const LoopRing &ring : rings_) cutRing(ring);
    // La faccia deve stare a sinistra nel piano (u, v): se la sua normale e'
    // opposta a Su x Sv i loop si girano.
    if (!face_.sense) {
        for (Chain &chain : chains_) {
            std::reverse(chain.points.begin(), chain.points.end());
            std::swap(chain.start, chain.end);
        }
        for (ClosedRing &ring : closed_) std::reverse(ring.points.begin(), ring.points.end());
    }
    // Faccia chiusa (sfera, toro) o con soli fori: le celle senza bordo sono dentro.
    bool hasOuter = !chains_.empty();
    for (const ClosedRing &ring : closed_) hasOuter = hasOuter || signedArea(ring.points) > 0.0;
    if (!hasOuter) {
        const bool closedU = periodic_[0] && (periodic_[1] || surface_.vDomain().isFinite());
        const bool closedV = periodic_[1] && (periodic_[0] || surface_.uDomain().isFinite());
        if (!closedU && !closedV) throw std::domain_error("tessellate: faccia senza contorno esterno");
    }
    for (int c = 0; c < cellCount(); ++c) {
        const Cell box = cell(c);
        std::vector<std::size_t> chains;
        for (std::size_t k = 0; k < chains_.size(); ++k)
            if (chains_[k].cell == c) chains.push_back(k);
        std::vector<std::vector<BoundaryPoint>> polygons = walkChains(chains, box);
        for (ClosedRing &ring : closed_)
            if (ring.cell == c) polygons.push_back(std::move(ring.points));
        if (!hasOuter) polygons.push_back(rectangle(box));
        triangulate(polygons, box.center());
    }
    refine();

    FaceMesh mesh;
    mesh.face = faceId_;
    mesh.points = std::move(points_);
    mesh.normals = std::move(normals_);
    mesh.parameters = std::move(uv_);
    if (!face_.sense)
        for (Vec3 &normal : mesh.normals) normal = -normal;
    mesh.triangles = std::move(triangles_);
    if (!face_.sense)
        for (std::array<int, 3> &t : mesh.triangles) std::swap(t[1], t[2]);
    return mesh;
}

}

Tessellation tessellate(const Body &input, const TessellationOptions &options) {
    // Servono le SP-curve di tutte le fin.
    const Body *bodyPointer = &input;
    Body completed;
    for (FinId f : input.fins())
        if (!input.fin(f).pcurve) {
            completed = input;
            computePCurves(completed);
            bodyPointer = &completed;
            break;
        }
    const Body &body = *bodyPointer;

    Tessellation result;
    std::vector<std::vector<double>> edgeSamples;
    for (EdgeId e : body.edges()) {
        const Edge &edge = body.edge(e);
        if (std::size_t(e.index) >= edgeSamples.size()) edgeSamples.resize(std::size_t(e.index) + 1);
        if (!edge.curve) continue;
        edgeSamples[std::size_t(e.index)] = sampleCurve(*edge.curve, edge.range, options);
        std::vector<Vec3> polyline;
        for (double t : edgeSamples[std::size_t(e.index)]) polyline.push_back(edge.curve->point(t));
        result.edges.push_back(std::move(polyline));
    }
    for (FaceId f : body.faces()) {
        try {
            if (!body.face(f).surface) throw std::invalid_argument("tessellate: faccia senza superficie");
            result.faces.push_back(FaceTessellator(body, f, edgeSamples, options).run());
        } catch (const std::exception &) {
            ++result.failedFaces;
        }
    }
    return result;
}

}
