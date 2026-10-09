#include "fk_boolean.h"
#include "fk_curve_surface.h"

#include <algorithm>
#include <exception>
#include <cmath>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <stdexcept>

#include "fk_body_check.h"
#include "fk_classify.h"
#include "fk_curve_ops.h"
#include "fk_curve_algo.h"
#include "fk_intersect.h"
#include "fk_marching.h"
#include "fk_parallel.h"
#include "fk_pcurve.h"
#include "fk_surface_algo.h"
#include "fk_unify.h"

namespace ForgeCad::Kernel {
namespace {

// Tratto di un arco d'intersezione che sta in entrambe le facce (o sul loro bordo).
struct Arc {
    CurvePtr<3> curve;
    Interval range;
    FaceId face[2];
    bool cut[2] = {false, false};  // divide l'interno della faccia del body 0 / 1
    bool coplanar = false;         // viene da due facce sulla stessa superficie (complanari, cilindri coassiali, ...)
    // Estremo dove la curva attraversa un edge tollerante (file importati):
    // il punto sull'edge, che fa da vertice, e lo scarto dalla curva. 0: nessuno.
    Vec3 loPoint, hiPoint;
    double loTolerance = 0.0, hiTolerance = 0.0;
};

// Punto di divisione di una curva d'intersezione preso su un edge tollerante:
// il vertice nuovo sta sull'edge (parameter = il punto della curva piu' vicino).
struct Snap {
    double parameter;
    Vec3 point;
    double tolerance;
};

// Tratto di curva percorso in un verso: pezzo di bordo o taglio.
struct Piece {
    CurvePtr<3> curve;
    Interval range;
    bool forward = true;
    double tolerance = 0.0;
    bool cut = false;
    FaceId partner;  // per i tagli: la faccia dell'altro body che taglia
    bool partnerCrosses = false;  // il taglio sta dentro la faccia che taglia (non sul suo bordo)
    bool coplanar = false;
    // Estremi in un vertice tollerante del body (file importati: le curve che
    // vi arrivano si fermano entro la tolleranza del vertice, non nello stesso
    // punto): il nodo e' il vertice, e vi si uniscono i punti entro loTolerance
    // (inizio della curva, range.lo) o hiTolerance (fine). 0: il punto della curva.
    Vec3 loVertex, hiVertex;
    double loTolerance = 0.0, hiTolerance = 0.0;

    double middleParameter() const { return 0.5 * (range.lo + range.hi); }
    Vec3 start() const { return curve->point(forward ? range.lo : range.hi); }
    Vec3 end() const { return curve->point(forward ? range.hi : range.lo); }
    Vec3 middle() const { return curve->point(middleParameter()); }
    Vec3 tangentAt(double t) const {
        const Vec3 d = curve->derivative(t);
        return forward ? d : -d;
    }
    Vec3 startTangent() const { return tangentAt(forward ? range.lo : range.hi); }
    Vec3 endTangent() const { return tangentAt(forward ? range.hi : range.lo); }
    // Punto a una frazione del tratto dall'inizio, nel verso di percorrenza.
    Vec3 pointAt(double fraction) const {
        return curve->point(forward ? range.lo + fraction * range.length() : range.hi - fraction * range.length());
    }
    // Nodo di un estremo nel verso di percorrenza: punto e tolleranza per unirlo.
    Vec3 startNode() const {
        if (forward ? loTolerance > 0.0 : hiTolerance > 0.0) return forward ? loVertex : hiVertex;
        return start();
    }
    Vec3 endNode() const {
        if (forward ? hiTolerance > 0.0 : loTolerance > 0.0) return forward ? hiVertex : loVertex;
        return end();
    }
    double startTolerance() const { return forward ? loTolerance : hiTolerance; }
    double endTolerance() const { return forward ? hiTolerance : loTolerance; }
    Piece reversed() const {
        Piece p = *this;
        p.forward = !forward;
        return p;
    }
};

enum class Location { In, Out, OnSame, OnOpposite };

struct Cycle {
    std::vector<Piece> pieces;
    std::vector<Vec2> polygon;  // nello spazio (u, v), srotolato
    int wrap = 0;               // giri in u (superfici periodiche in u)
    int wrapV = 0;              // giri in v (superfici periodiche in v, il toro)
    double area = 0.0, meanV = 0.0, meanU = 0.0;  // quota media dei cicli avvolti in u (v) e in v (u)
};

struct SubFace {
    int body = 0;
    FaceId face;
    std::vector<Cycle> cycles;
    // Senza contorno esterno: tutta la superficie chiusa (sfera, toro) meno
    // gli eventuali fori; nessun ciclo: la superficie intera.
    bool whole = false;
};

bool isPlane(const Surface &s) { return s.type() == SurfaceType::Plane; }

// Normale uscente della faccia in x. Nei poli (sfera, vertice del cono) la
// normale dell'anello attorno, dalla parte di `toward` (un punto della
// faccia vicino): nel vertice del cono le due falde hanno normali opposte.
Vec3 faceNormal(const Surface &surface, bool sense, const Vec3 &x, double tolerance = 1e-6, const Vec3 *toward = nullptr) {
    const SurfaceProjection projection = projectPoint(surface, x);
    const std::vector<SurfacePole> poles = surfacePoles(surface);
    const int pole = poleIndex(poles, x, tolerance);
    Vec3 n;
    if (pole >= 0) {
        int side = 0;
        if (toward) side = projectPoint(surface, *toward).v > poles[pole].v ? 1 : -1;
        n = normalAt(surface, projection.u, poles[pole].v, side);
    } else {
        n = normalAt(surface, projection.u, projection.v);
    }
    return sense ? n : -n;
}

class BooleanBuilder {
public:
    BooleanBuilder(const Body &a, const Body &b, BooleanOperation operation, const BooleanOptions &options, bool split = false)
        : operation_(operation), tolerance_(options.tolerance), unify_(options.unifySameDomain), split_(split), threads_(options.threads) {
        bodies_[0] = a;
        bodies_[1] = b;
        Box all;
        for (int k = 0; k < 2; ++k) {
            // Senza SP-curve una fin non si puo' tagliare ne' classificare: errore
            // invece di dereferenziare un puntatore nullo nei passi successivi.
            if (computePCurves(bodies_[k]) > 0)
                for (FaceId f : bodies_[k].faces())
                    for (LoopId l : bodies_[k].face(f).loops)
                        for (FinId fin : bodies_[k].loopFins(l))
                            if (!bodies_[k].fin(fin).pcurve)
                                throw std::domain_error("booleanOperation: SP-curve non calcolabile sull'edge E"
                                                        + std::to_string(bodies_[k].fin(fin).edge.index) + " della faccia F"
                                                        + std::to_string(f.index) + " del corpo " + (k == 0 ? "A" : "B"));
            for (FaceId f : bodies_[k].faces()) {
                if (!bodies_[k].face(f).surface) throw std::invalid_argument("booleanOperation: faccia senza superficie");
                boxes_[k][f.index] = faceBox(bodies_[k], f).padded(tolerance_);
                all.add(boxes_[k][f.index]);
            }
            for (VertexId v : bodies_[k].vertices()) vertexTolerance_ = std::max(vertexTolerance_, bodies_[k].vertex(v).tolerance);
            for (EdgeId e : bodies_[k].edges()) vertexTolerance_ = std::max(vertexTolerance_, bodies_[k].edge(e).tolerance);
        }
        scale_ = std::max(all.diagonal(), 1.0);
    }

    Body run();
    // Divisione della lamina (body 0) lungo le curve in cui la attraversa il body 1: un body per regione connessa.
    std::vector<Body> split();

private:
    // SP-curve delle curve d'intersezione approssimate, per curva e superficie.
    struct PCurves {
        const Surface *surface[2];
        CurvePtr<2> pcurve[2];
        double deviation;
    };
    // Risultato del passo 1 per una coppia di facce. Le coppie si calcolano
    // in parallelo, ognuna nel suo PairResult; poi si uniscono nell'ordine
    // delle coppie, come se fossero state calcolate una dopo l'altra.
    struct PairResult {
        std::vector<Arc> arcs;
        std::map<const Curve<3> *, PCurves> pcurves;
        // Superfici che coincidono solo in parte e i tagli della loro zona comune.
        std::set<std::pair<const Surface *, const Surface *>> partial;
        std::vector<IntersectionCurve> partialCuts;
        // Tagli delle zone comuni delle coppie precedenti (a cui si agganciano
        // gli estremi dei tratti comuni degli edge sulle B-spline) e se sono serviti.
        const std::vector<IntersectionCurve> *earlierCuts = nullptr;
        bool usedEarlierCuts = false;
        std::exception_ptr error;
    };
    // Tagli di ogni faccia (pezzi degli archi d'intersezione) dopo il passo 1.
    void computeCuts(std::map<int, std::vector<Piece>> (&cuts)[2]);
    // Estremi quasi coincidenti degli archi resi un vertice tollerante.
    void snapNearEnds();
    // Passo 5: cucitura dei pezzi tenuti ("da girare" se il secondo e' vero).
    Body assemble(const std::vector<std::pair<SubFace, bool>> &kept, bool sheetResult);
    void pairArcs(FaceId fa, FaceId fb, PairResult &out) const;
    bool coincident(const Surface &a, const Surface &b) const;
    void coincidentArcs(FaceId fa, FaceId fb, PairResult &out, bool partial = false) const;
    std::vector<Interval> coincidentRanges(const Curve<3> &curve, const Interval &range, const Surface &surface, PairResult &out) const;
    void surfaceArcs(FaceId fa, FaceId fb, PairResult &out) const;
    Box commonBounds(FaceId fa, FaceId fb) const;
    bool touchesBoth(FaceId fa, FaceId fb, const Vec3 &x) const;
    void addArcs(const CurvePtr<3> &curve, const Interval &range, const std::vector<double> &parameters, FaceId fa, FaceId fb,
                 PairResult &out, const IntersectionCurve *source = nullptr, const std::vector<Snap> &snaps = {}) const;
    std::vector<Interval> splitRange(const Curve<3> &curve, const Interval &range, std::vector<double> parameters) const;
    std::vector<Interval> splitAtPoints(const Curve<3> &curve, const Interval &range) const;
    std::vector<SubFace> buildSubFaces(int k, FaceId f, const std::vector<Piece> &cuts) const;
    std::vector<SubFace> buildSubFacesOnce(int k, FaceId f, const std::vector<Piece> &cuts, int polygonSamples) const;
    void finishCycle(const Surface &surface, bool sense, Cycle &cycle, int polygonSamples) const;
    bool insideSubFace(const SubFace &subFace, const Vec2 &uv) const;
    Location classify(const SubFace &subFace, const SolidClassifier &other) const;
    bool keep(int k, Location location) const;
    void pairRadially(const std::vector<Vec3> &vertices, std::vector<Body::BuildEdge> &edges, std::vector<Vec3> &middles,
                      std::vector<Body::BuildFace> &faces) const;
    static void splitVertices(std::vector<Vec3> &vertices, std::vector<Body::BuildEdge> &edges, const std::vector<Body::BuildFace> &faces);

    Body bodies_[2];
    BooleanOperation operation_;
    double tolerance_, scale_ = 1.0;
    double vertexTolerance_ = 0.0;  // la piu' grande tolleranza dei vertici dei due body (file importati)
    bool unify_ = true, split_ = false;
    int threads_ = 0;
    std::map<int, Box> boxes_[2];
    std::vector<Arc> arcs_;
    std::vector<Vec3> vertexPoints_;
    std::map<const Curve<3> *, PCurves> pcurves_;
    mutable std::map<std::pair<const Surface *, const Surface *>, bool> coincident_;  // sameSurface gia' calcolati
    mutable std::mutex coincidentMutex_;
    // Superfici che coincidono solo in parte (fianchi estrusi in direzioni
    // parallele con sezioni sovrapposte in un tratto), nei due ordini.
    std::set<std::pair<const Surface *, const Surface *>> partial_;
    std::vector<IntersectionCurve> partialCuts_;
};

// --- 1. archi d'intersezione ----------------------------------------------------

std::vector<Interval> BooleanBuilder::splitRange(const Curve<3> &curve, const Interval &range, std::vector<double> parameters) const {
    const bool closed = curve.isPeriodic() && range.length() >= curve.period() - 1e-12;
    std::sort(parameters.begin(), parameters.end());
    std::vector<Interval> result;
    if (closed) {
        const double period = curve.period();
        for (double &t : parameters) t = range.lo + std::fmod(std::fmod(t - range.lo, period) + period, period);
        std::sort(parameters.begin(), parameters.end());
        std::vector<double> unique;
        for (double t : parameters)
            if (unique.empty() || distance(curve.point(t), curve.point(unique.back())) > tolerance_) unique.push_back(t);
        if (unique.size() > 1 && distance(curve.point(unique.front()), curve.point(unique.back())) <= tolerance_) unique.pop_back();
        if (unique.empty()) return {range};
        for (std::size_t i = 0; i < unique.size(); ++i)
            result.push_back({unique[i], i + 1 < unique.size() ? unique[i + 1] : unique.front() + period});
        return result;
    }
    // Punti interni distinti; un estremo del tratto vicino a uno di questi
    // viene sostituito dal punto (niente tratti lunghi quanto la tolleranza).
    for (double &t : parameters) t = range.clamp(t);
    std::sort(parameters.begin(), parameters.end());
    // Stesso punto se anche la curva tra i due parametri resta li' (una curva
    // che torna nello stesso punto dopo un giro va divisa in entrambi).
    auto samePoint = [&](double s, double t) {
        return distance(curve.point(s), curve.point(t)) <= 2.0 * tolerance_ && distance(curve.point(0.5 * (s + t)), curve.point(s)) <= 2.0 * tolerance_;
    };
    std::vector<double> unique;
    for (double t : parameters)
        if (unique.empty() || !samePoint(unique.back(), t)) unique.push_back(t);
    if (unique.empty() || !samePoint(range.lo, unique.front())) unique.insert(unique.begin(), range.lo);
    if (!samePoint(unique.back(), range.hi) || unique.size() == 1) unique.push_back(range.hi);
    for (std::size_t i = 0; i + 1 < unique.size(); ++i) result.push_back({unique[i], unique[i + 1]});
    return result;
}

bool BooleanBuilder::coincident(const Surface &a, const Surface &b) const {
    const auto key = std::make_pair(&a, &b);
    {
        const std::lock_guard<std::mutex> lock(coincidentMutex_);
        const auto found = coincident_.find(key);
        if (found != coincident_.end()) return found->second;
    }
    bool same = sameSurface(a, b, tolerance_);
    // Piani quasi paralleli che dentro i corpi si scostano meno della
    // tolleranza (normali giuste solo all'ultimo bit, come i piani equivalenti
    // delle B-spline): lo stesso piano, come per intersectPlaneSurface.
    if (!same && isPlane(a) && isPlane(b)) {
        const Frame3 &fa = static_cast<const Plane &>(a).frame(), &fb = static_cast<const Plane &>(b).frame();
        same = norm(cross(fa.zDir(), fb.zDir())) * scale_ <= tolerance_ && std::fabs(dot(fa.zDir(), fb.origin() - fa.origin())) <= tolerance_;
    }
    const std::lock_guard<std::mutex> lock(coincidentMutex_);
    return coincident_[key] = same;
}

void BooleanBuilder::pairArcs(FaceId fa, FaceId fb, PairResult &out) const {
    const Surface &sa = *bodies_[0].face(fa).surface, &sb = *bodies_[1].face(fb).surface;
    if (coincident(sa, sb)) {
        coincidentArcs(fa, fb, out);
        return;
    }
    int k;
    if (isPlane(sa)) k = 0;
    else if (isPlane(sb)) k = 1;
    else {
        surfaceArcs(fa, fb, out);
        return;
    }
    // Piano con cono, toro, rivoluzione o B-spline: curve tracciate come tra
    // due superfici qualsiasi (semi dalla suddivisione).
    const FaceId fp = k == 0 ? fa : fb, fq = k == 0 ? fb : fa;
    const Body &bp = bodies_[k], &bq = bodies_[1 - k];
    const Plane &plane = static_cast<const Plane &>(*bp.face(fp).surface);
    const Vec3 n = plane.frame().zDir();
    const double offset = dot(n, plane.frame().origin());
    const Surface &curved = *bq.face(fq).surface;
    const SurfaceType other = curved.type();
    bool exact = other == SurfaceType::Plane || other == SurfaceType::Cylinder || other == SurfaceType::Sphere || other == SurfaceType::Extrusion;
    if (other == SurfaceType::Cone) {
        // Piano per il vertice (generatrici) o perpendicolare all'asse (cerchio): curve esatte.
        const auto &cone = static_cast<const ConicalSurface &>(curved);
        exact = std::fabs(dot(n, cone.apex()) - offset) <= tolerance_ || std::fabs(dot(n, cone.frame().zDir())) >= 1.0 - 1e-15;
    }
    if (!exact) {
        surfaceArcs(fa, fb, out);
        return;
    }
    const PlaneSurfaceIntersection intersection = intersectPlaneSurface(plane, curved, commonBounds(fa, fb), tolerance_);
    if (intersection.coincident) throw std::domain_error("booleanOperation: superficie non piana che contiene parte di un piano");
    // Le rette di tangenza (il piano tocca la superficie senza attraversarla)
    // non dividono le facce: ogni pezzo resta tutto da una parte dell'altro solido.

    for (std::size_t i = 0; i < intersection.curves.size(); ++i) {
        const CurvePtr<3> &curve = intersection.curves[i];
        const Interval &range = intersection.ranges[i];
        std::vector<double> parameters;
        std::vector<Snap> snaps;
        // Attraversamenti del bordo della faccia piana (nel piano).
        const CurvePtr<2> planar = exactPCurve(plane, curve, range, std::max(tolerance_, 1e-9 * scale_));
        if (!planar) throw std::logic_error("booleanOperation: curva d'intersezione fuori dal piano");
        for (LoopId l : bp.face(fp).loops)
            for (FinId f : bp.loopFins(l)) {
                const Edge &edge = bp.edge(bp.fin(f).edge);
                for (const CurveCurvePoint &p : intersectCurves(*planar, range, *bp.fin(f).pcurve, edge.range, tolerance_).points)
                    parameters.push_back(p.s);
            }
        // Attraversamenti del bordo dell'altra faccia: i suoi edge sul piano.
        for (LoopId l : bq.face(fq).loops)
            for (FinId f : bq.loopFins(l)) {
                const Edge &edge = bq.edge(bq.fin(f).edge);
                const PlaneRoots<3> roots = planeRoots<3>(*edge.curve, edge.range, n, offset, tolerance_);
                std::vector<Vec3> points;
                for (double t : roots.parameters) points.push_back(edge.curve->point(t));
                for (const Interval &piece : roots.coincident) {
                    points.push_back(edge.curve->point(piece.lo));
                    points.push_back(edge.curve->point(piece.hi));
                }
                for (const Vec3 &x : points) {
                    const CurveProjection<3> projection = projectPoint(*curve, x, range);
                    if (projection.distance > 10.0 * tolerance_ + 2.0 * edge.tolerance) continue;
                    parameters.push_back(projection.parameter);
                    // Punto sull'edge da cui la curva passa solo vicino (edge tolleranti dei file).
                    if (projection.distance > 0.1 * tolerance_)
                        snaps.push_back({projection.parameter, x, std::max(tolerance_, 1.01 * projection.distance)});
                }
            }
        addArcs(curve, range, parameters, fa, fb, out, nullptr, snaps);
    }
}

// Box comune alle due facce, allargato ben oltre la tolleranza: le curve
// vengono limitate a questo box e un estremo artificiale vicino a un vertice
// vero si confonderebbe con lui.
Box BooleanBuilder::commonBounds(FaceId fa, FaceId fb) const {
    const Box &boxA = boxes_[0].at(fa.index), &boxB = boxes_[1].at(fb.index);
    Box bounds;
    for (int i = 0; i < 3; ++i) {
        bounds.lo[i] = std::max(boxA.lo[i], boxB.lo[i]);
        bounds.hi[i] = std::min(boxA.hi[i], boxB.hi[i]);
    }
    return bounds.padded(1e-3 * scale_ + 100.0 * tolerance_);
}

bool BooleanBuilder::touchesBoth(FaceId fa, FaceId fb, const Vec3 &x) const {
    return classifyPointOnFace(bodies_[0], fa, x, tolerance_) != PointLocation::Outside
        && classifyPointOnFace(bodies_[1], fb, x, tolerance_) != PointLocation::Outside;
}

// Tratti della curva (divisa nei parametri dati) che stanno in entrambe le facce.
void BooleanBuilder::addArcs(const CurvePtr<3> &curve, const Interval &range, const std::vector<double> &parameters, FaceId fa,
                             FaceId fb, PairResult &out, const IntersectionCurve *source, const std::vector<Snap> &snapsIn) const {
    // Punti presi su edge tolleranti: le altre divisioni entro la loro
    // tolleranza (per esempio l'estremo della curva tracciata dallo stesso
    // punto, raffinato altrove) sono lo stesso punto; i tratti che restano tra
    // il punto e un estremo della curva cosi' vicino si scartano (sotto).
    std::vector<double> splits = parameters;
    const std::vector<Snap> &snaps = snapsIn;
    for (const Snap &snap : snaps) {
        const Vec3 at = curve->point(snap.parameter);
        const double reach = 2.0 * snap.tolerance + tolerance_;
        splits.erase(std::remove_if(splits.begin(), splits.end(), [&](double t) { return distance(curve->point(t), at) <= reach; }),
                     splits.end());
        splits.push_back(snap.parameter);
    }
    const auto snapAt = [&](double t) -> const Snap * {
        const Snap *best = nullptr;
        double bestGap = tolerance_;
        for (const Snap &snap : snaps) {
            const double gap = distance(curve->point(t), curve->point(snap.parameter));
            if (gap <= bestGap) {
                bestGap = gap;
                best = &snap;
            }
        }
        return best;
    };
    bool added = false;
    // Le curve che passano per un polo di una delle due superfici (sfera,
    // vertice del cono) vi hanno un vertice: nello spazio (u, v) il polo e'
    // una linea e l'SP-curve li' salta.
    for (const Surface *surface : {bodies_[0].face(fa).surface.get(), bodies_[1].face(fb).surface.get()})
        for (const SurfacePole &pole : surfacePoles(*surface)) {
            const CurveProjection<3> projection = projectPoint(*curve, pole.point, range);
            if (projection.distance <= 10.0 * tolerance_) splits.push_back(projection.parameter);
        }
    for (const Interval &piece : splitRange(*curve, range, splits)) {
        const Vec3 middle = curve->point(0.5 * (piece.lo + piece.hi));
        // Tratto degenere (una curva chiusa intera ha il punto medio lontano dagli estremi).
        if (distance(curve->point(piece.lo), curve->point(piece.hi)) <= tolerance_ && distance(middle, curve->point(piece.lo)) <= tolerance_)
            continue;
        // Tratti fuori dal dominio di una superficie (le curve tracciate sulle
        // B-spline possono proseguire dove la superficie e' solo estrapolata):
        // la proiezione cadrebbe sul bordo della faccia e sembrerebbero dentro.
        // Le curve d'intersezione stanno sulle due superfici entro 1e-9 (fk_marching).
        const double onSurface = 10.0 * tolerance_;
        if (projectPoint(*bodies_[0].face(fa).surface, middle).distance > onSurface
            || projectPoint(*bodies_[1].face(fb).surface, middle).distance > onSurface)
            continue;
        const Snap *loSnap = snaps.empty() ? nullptr : snapAt(piece.lo), *hiSnap = snaps.empty() ? nullptr : snapAt(piece.hi);
        if (loSnap || hiSnap) {
            const double reach = 2.0 * std::max(loSnap ? loSnap->tolerance : 0.0, hiSnap ? hiSnap->tolerance : 0.0) + tolerance_;
            if (distance(curve->point(piece.lo), curve->point(piece.hi)) <= reach && distance(middle, curve->point(piece.lo)) <= reach) continue;
            // Tratto tra il punto sull'edge tollerante e l'estremo della curva sul
            // bordo della superficie (dove il piano taglia l'edge di striscio si
            // allungano): sta lungo il bordo della faccia, entro la tolleranza degli edge.
            bool alongBoundary = false;
            for (int k = 0; k < 2 && !alongBoundary; ++k) {
                const Body &body = bodies_[k];
                const FaceId face = k == 0 ? fa : fb;
                double edgeTolerance = 0.0;
                for (LoopId l : body.face(face).loops)
                    for (FinId f : body.loopFins(l)) edgeTolerance = std::max(edgeTolerance, body.edge(body.fin(f).edge).tolerance);
                if (edgeTolerance > tolerance_) {
                    const double reachBoundary = 2.0 * edgeTolerance;
                    alongBoundary = distanceToFaceBoundary(body, face, middle) <= reachBoundary
                                 && distanceToFaceBoundary(body, face, curve->point(piece.lo)) <= reachBoundary
                                 && distanceToFaceBoundary(body, face, curve->point(piece.hi)) <= reachBoundary;
                }
            }
            if (alongBoundary) continue;
        }
        const PointLocation in0 = classifyPointOnFace(bodies_[0], fa, middle, tolerance_);
        if (in0 == PointLocation::Outside) continue;
        const PointLocation in1 = classifyPointOnFace(bodies_[1], fb, middle, tolerance_);
        if (in1 == PointLocation::Outside) continue;
        Arc arc;
        arc.curve = curve;
        arc.range = piece;
        arc.face[0] = fa;
        arc.face[1] = fb;
        arc.cut[0] = in0 == PointLocation::Inside;
        arc.cut[1] = in1 == PointLocation::Inside;
        if (loSnap) {
            arc.loPoint = loSnap->point;
            arc.loTolerance = loSnap->tolerance;
        }
        if (hiSnap) {
            arc.hiPoint = hiSnap->point;
            arc.hiTolerance = hiSnap->tolerance;
        }
        out.arcs.push_back(arc);
        added = true;
    }
    if (added && source && source->pcurves[0]) {
        PCurves entry;
        entry.surface[0] = bodies_[0].face(fa).surface.get();
        entry.surface[1] = bodies_[1].face(fb).surface.get();
        entry.pcurve[0] = source->pcurves[0];
        entry.pcurve[1] = source->pcurves[1];
        entry.deviation = source->deviation;
        out.pcurves[curve.get()] = entry;
    }
}

// Due superfici non piane: curve d'intersezione tracciate (fk_marching) a
// partire dai punti in cui gli edge di una faccia attraversano la superficie
// dell'altra dentro di essa.
void BooleanBuilder::surfaceArcs(FaceId fa, FaceId fb, PairResult &out) const {
    std::vector<Vec3> crossings;
    std::vector<double> crossingTolerances;  // tolleranza dell'edge da cui viene il punto (-1: non viene da un edge)
    auto collect = [&](int k, FaceId self, FaceId otherFace) {
        const Body &body = bodies_[k], &other = bodies_[1 - k];
        const Surface &otherSurface = *other.face(otherFace).surface;
        for (LoopId l : body.face(self).loops)
            for (FinId f : body.loopFins(l)) {
                const Edge &edge = body.edge(body.fin(f).edge);
                const double edgeTolerance = edge.tolerance;
                const auto cross = [&](const Vec3 &x) {
                    if (classifyPointOnFace(other, otherFace, x, tolerance_) == PointLocation::Outside) return;
                    crossings.push_back(x);
                    crossingTolerances.push_back(edgeTolerance);
                };
                const CurveSurfaceIntersection hits = intersectCurveSurface(*edge.curve, edge.range, otherSurface, tolerance_);
                // Un edge che giace sull'altra superficie (per esempio una
                // generatrice comune a due fianchi estrusi nella stessa
                // direzione) e' anche un tratto della curva d'intersezione:
                // i suoi estremi dividono le curve come i punti di attraversamento.
                for (const Interval &piece : hits.coincident)
                    for (double t : {piece.lo, piece.hi}) cross(edge.curve->point(t));
                for (double t : hits.parameters) cross(edge.curve->point(t));
            }
    };
    collect(0, fa, fb);
    collect(1, fb, fa);

    SurfaceIntersectionOptions options;
    options.tolerance = tolerance_;
    const Surface &surfaceA = *bodies_[0].face(fa).surface, &surfaceB = *bodies_[1].face(fb).surface;
    const SurfaceIntersection intersection = intersectSurfaces(surfaceA, surfaceB, commonBounds(fa, fb), crossings, options);
    if (intersection.coincident) {
        // Solo i fianchi estrusi in direzioni parallele (le rette agli estremi
        // della striscia comune sono gia' tra le curve).
        GeneralizedCylinder ga, gb;
        const Surface &sa = *bodies_[0].face(fa).surface, &sb = *bodies_[1].face(fb).surface;
        const bool parallel = generalizedCylinder(sa, ga) && generalizedCylinder(sb, gb) && norm(cross(ga.direction, gb.direction)) <= 1e-12;
        const bool spline = sa.type() == SurfaceType::BSpline || sb.type() == SurfaceType::BSpline;
        if (!parallel && !spline && !coaxialRotational(sa, sb, tolerance_))
            throw std::domain_error("booleanOperation: superfici non piane coincidenti in parte non gestite");
        // Tagli della zona comune (linee di nodo): gli estremi dei tratti
        // comuni degli edge vi si agganciano (dove una B-spline si separa
        // dall'altra con continuita' alta lo scarto cresce lentamente).
        for (const IntersectionCurve &cut : intersection.curves) out.partialCuts.push_back(cut);
        out.partial.insert({&sa, &sb});
        out.partial.insert({&sb, &sa});
        coincidentArcs(fa, fb, out, true);
    }
    // Contatti di ordine superiore (le seconde forme non bastano a decidere):
    // su un anello attorno al punto, sulla superficie A, la distanza con segno
    // da B non cambia segno se le superfici si toccano soltanto (contatto
    // isolato: non divide le facce). Se cambia ne partono rami: vanno bene
    // solo se una curva tracciata passa per il punto (lo divide come un
    // incrocio); un tracciamento fermato vicino al punto lascerebbe il taglio aperto.
    for (const Vec3 &x : intersection.tangentPoints) {
        const PointLocation onFaceA = classifyPointOnFace(bodies_[0], fa, x, tolerance_);
        const PointLocation onFaceB = classifyPointOnFace(bodies_[1], fb, x, tolerance_);
        if (onFaceA == PointLocation::Outside || onFaceB == PointLocation::Outside) continue;
        const Vec3 n = faceNormal(surfaceA, true, x, tolerance_);
        const Vec3 e1 = normalized(std::fabs(n.x()) < 0.9 ? cross(n, Vec3(1, 0, 0)) : cross(n, Vec3(0, 1, 0))), e2 = cross(n, e1);
        bool positive = false, negative = false;
        for (double radius : {1e-4 * scale_, 1e-3 * scale_, 1e-2 * scale_})
            for (int k = 0; k < 32; ++k) {
                const double angle = kTwoPi * k / 32.0;
                const Vec3 onA = projectPoint(surfaceA, x + radius * (std::cos(angle) * e1 + std::sin(angle) * e2)).point;
                const SurfaceProjection onB = projectPoint(surfaceB, onA);
                const double d = dot(onA - onB.point, normalAt(surfaceB, onB.u, onB.v));
                positive = positive || d > tolerance_;
                negative = negative || d < -tolerance_;
            }
        if (!positive || !negative) continue;
        bool through = false;
        for (const IntersectionCurve &curve : intersection.curves) {
            const CurveProjection<3> projection = projectPoint(*curve.curve, x, curve.range);
            const double margin = 1e-6 * curve.range.length();
            through = through || (projection.distance <= 10.0 * tolerance_ && (curve.closed || (projection.parameter > curve.range.lo + margin
                                                                                               && projection.parameter < curve.range.hi - margin)));
        }
        // Rami che arrivano nel punto (completati dal tracciamento): almeno due estremi.
        int ends = 0;
        for (const IntersectionCurve &curve : intersection.curves)
            if (!curve.closed)
                for (double t : {curve.range.lo, curve.range.hi}) ends += distance(curve.curve->point(t), x) <= 10.0 * tolerance_;
        through = through || ends >= 2;
        // L'anello appartiene alle superfici illimitate e puo' vedere rami che
        // non diventano un taglio nelle facce finite. Gli edge hanno gia'
        // fornito i semi e le divisioni necessarie: se il tracciamento non ha
        // prodotto ne' un ramo passante ne' due estremi, resta un contatto e
        // non deve far fallire tutta l'operazione (caso tipico della testa di
        // una sweep appoggiata al bordo del solido bersaglio).
        if (!through) continue;
        crossings.push_back(x);
    }
    // Contatti isolati e rette di tangenza non dividono le facce; nei punti
    // in cui due rami si incrociano le curve vanno spezzate.
    for (const Vec3 &x : intersection.singularPoints)
        if (touchesBoth(fa, fb, x)) crossings.push_back(x);

    // Curve che si incrociano tra loro (punti di tangenza delle superfici
    // non segnalati dal tracciamento, per esempio i cerchi di Villarceau di un
    // piano bitangente a un toro, tracciati come curve chiuse): i punti
    // comuni, dalle SP-curve su una superficie non periodica se c'e'.
    const Surface *surfaces[2] = {bodies_[0].face(fa).surface.get(), bodies_[1].face(fb).surface.get()};
    const int flat = !surfaces[0]->isUPeriodic() && !surfaces[0]->isVPeriodic() ? 0 : 1;
    for (std::size_t i = 0; i < intersection.curves.size(); ++i)
        for (std::size_t j = i + 1; j < intersection.curves.size(); ++j) {
            const IntersectionCurve &ci = intersection.curves[i], &cj = intersection.curves[j];
            if (!ci.pcurves[flat] || !cj.pcurves[flat]) continue;
            const Surface &surface = *surfaces[flat];
            std::vector<CurvePtr<2>> images{cj.pcurves[flat]};
            for (int d = 0; d < 2; ++d) {
                const bool periodic = d == 0 ? surface.isUPeriodic() : surface.isVPeriodic();
                if (!periodic) continue;
                const double period = d == 0 ? surface.uPeriod() : surface.vPeriod();
                const std::size_t n = images.size();
                for (std::size_t m = 0; m < n; ++m)
                    for (double shift : {-period, period}) {
                        Vec2 offset;
                        offset[d] = shift;
                        images.push_back(translatedCurve<2>(images[m], offset));
                    }
            }
            for (const CurvePtr<2> &image : images)
                for (const CurveCurvePoint &p : intersectCurves(*ci.pcurves[flat], ci.range, *image, cj.range, tolerance_).points) {
                    const Vec3 x = ci.curve->point(p.s);
                    // Gli estremi comuni (due curve che finiscono nello stesso punto) sono gia' vertici.
                    bool atEnd = false;
                    for (const IntersectionCurve *c : {&ci, &cj})
                        for (double t : {c->range.lo, c->range.hi}) atEnd = atEnd || distance(x, c->curve->point(t)) <= 100.0 * tolerance_;
                    if (!atEnd && distance(x, cj.curve->point(p.t)) <= 10.0 * tolerance_ && touchesBoth(fa, fb, x)) crossings.push_back(x);
                }
        }

    crossingTolerances.resize(crossings.size(), -1.0);
    for (const IntersectionCurve &curve : intersection.curves) {
        std::vector<double> parameters = curve.splitParameters;
        std::vector<Snap> snaps;
        for (std::size_t i = 0; i < crossings.size(); ++i) {
            const Vec3 &x = crossings[i];
            const CurveProjection<3> projection = projectPoint(*curve.curve, x, curve.range);
            const double edgeTolerance = crossingTolerances[i];
            if (projection.distance > 10.0 * tolerance_ + 2.0 * std::max(edgeTolerance, 0.0)) continue;
            parameters.push_back(projection.parameter);
            // Punto su un edge da cui la curva passa solo vicino (edge tolleranti dei
            // file, o curve che si fermano entro la tolleranza): il vertice e' il
            // punto sull'edge, lo stesso per le curve delle facce vicine.
            if (edgeTolerance >= 0.0 && projection.distance > 0.1 * tolerance_)
                snaps.push_back({projection.parameter, x, std::max(tolerance_, 1.01 * projection.distance)});
        }
        addArcs(curve.curve, curve.range, parameters, fa, fb, out, &curve, snaps);
    }
}

// Facce sulla stessa superficie (complanari, cilindri coassiali dello stesso
// raggio, fianchi estrusi dalla stessa curva): il bordo di ciascuna, dove
// passa dentro l'altra, la taglia. Gli attraversamenti tra i due bordi si
// cercano nello spazio (u, v) della faccia tagliata, con le SP-curve degli
// edge dell'altra portate su quella superficie (sulle superfici periodiche
// anche spostate di un periodo).
// Tratti della curva che stanno sulla superficie. Un cerchio coassiale con
// una superficie di rivoluzione ci sta tutto o per niente.
std::vector<Interval> BooleanBuilder::coincidentRanges(const Curve<3> &curve, const Interval &range, const Surface &surface, PairResult &out) const {
    if (curve.type() == CurveType::Circle) {
        const auto &circle = static_cast<const Circle<3> &>(curve);
        const Vec3 normal = cross(circle.xAxis(), circle.yAxis());
        if (coaxialRotational(CylindricalSurface(Frame3(circle.center(), normal, circle.xAxis()), circle.radius()), surface, tolerance_)) {
            for (double f : {0.0, 0.37, 0.71})
                if (projectPoint(surface, curve.point(range.lo + f * range.length())).distance > tolerance_) return {};
            return {range};
        }
    }
    if (surface.type() != SurfaceType::BSpline) return intersectCurveSurface(curve, range, surface, tolerance_).coincident;
    // B-spline: gli estremi si agganciano anche ai tagli della zona comune.
    std::vector<std::pair<CurvePtr<3>, Interval>> snap;
    out.usedEarlierCuts = true;
    if (out.earlierCuts)
        for (const IntersectionCurve &cut : *out.earlierCuts) snap.emplace_back(cut.curve, cut.range);
    for (const IntersectionCurve &cut : out.partialCuts) snap.emplace_back(cut.curve, cut.range);
    return curveOnSurface(curve, range, surface, tolerance_, snap);
}

void BooleanBuilder::coincidentArcs(FaceId fa, FaceId fb, PairResult &out, bool partial) const {
    for (int k = 0; k < 2; ++k) {
        const FaceId fx = k == 0 ? fa : fb, fy = k == 0 ? fb : fa;
        const Body &bx = bodies_[k], &by = bodies_[1 - k];
        const Surface &surface = *bx.face(fx).surface;
        const double accept = std::max(tolerance_, 1e-9 * scale_);
        for (LoopId l : by.face(fy).loops)
            for (FinId f : by.loopFins(l)) {
                const Edge &fullEdge = by.edge(by.fin(f).edge);
                // Superfici coincidenti in parte: solo i tratti dell'edge che stanno sull'altra.
                std::vector<Interval> ranges{fullEdge.range};
                if (partial) ranges = coincidentRanges(*fullEdge.curve, fullEdge.range, surface, out);
                for (const Interval &range : ranges) {
                Edge edge = fullEdge;
                edge.range = range;
                // Tratto degenere; un edge chiuso (il cerchio intero di una
                // rivoluzione) ha gli estremi uguali ma il punto medio lontano.
                if (!(range.length() > 0.0)
                    || (distance(edge.curve->point(range.lo), edge.curve->point(range.hi)) <= tolerance_
                        && distance(edge.curve->point(range.lo), edge.curve->point(0.5 * (range.lo + range.hi))) <= tolerance_))
                    continue;
                CurvePtr<2> pcurve = exactPCurve(surface, edge.curve, edge.range, accept);
                if (!pcurve && !isPlane(surface)) pcurve = fitPCurve(surface, edge.curve, edge.range, std::min(accept, kPCurveTolerance));
                if (!pcurve) throw std::logic_error("booleanOperation: edge fuori dalla superficie coincidente");
                std::vector<CurvePtr<2>> images{pcurve};
                for (int d = 0; d < 2; ++d) {
                    const bool periodic = d == 0 ? surface.isUPeriodic() : surface.isVPeriodic();
                    if (!periodic) continue;
                    const double period = d == 0 ? surface.uPeriod() : surface.vPeriod();
                    const std::size_t count = images.size();
                    for (std::size_t i = 0; i < count; ++i)
                        for (double shift : {-period, period}) {
                            Vec2 offset;
                            offset[d] = shift;
                            images.push_back(translatedCurve<2>(images[i], offset));
                        }
                }
                std::vector<double> parameters;
                for (LoopId lx : bx.face(fx).loops)
                    for (FinId fxFin : bx.loopFins(lx)) {
                        const Edge &other = bx.edge(bx.fin(fxFin).edge);
                        for (const CurvePtr<2> &image : images)
                            for (const CurveCurvePoint &p : intersectCurves(*image, edge.range, *bx.fin(fxFin).pcurve, other.range, tolerance_).points)
                                parameters.push_back(p.s);
                        // Un vertice del bordo sull'edge: l'edge vi esce o vi entra
                        // nella faccia anche se le intersezioni con i due edge del
                        // vertice cadono appena fuori dai loro tratti.
                        const Vertex &vertex = bx.vertex(bx.finStart(fxFin));
                        const CurveProjection<3> onEdge = projectPoint(*edge.curve, vertex.point, edge.range);
                        if (onEdge.distance <= std::max(tolerance_, vertex.tolerance)) parameters.push_back(onEdge.parameter);
                    }
                for (const Interval &piece : splitRange(*edge.curve, edge.range, parameters)) {
                    const Vec3 middle = edge.curve->point(0.5 * (piece.lo + piece.hi));
                    const PointLocation location = classifyPointOnFace(bx, fx, middle, tolerance_);
                    if (location == PointLocation::Outside) continue;
                    Arc arc;
                    arc.curve = edge.curve;
                    arc.range = piece;
                    arc.face[k] = fx;
                    arc.face[1 - k] = fy;
                    arc.cut[k] = location == PointLocation::Inside;
                    arc.coplanar = true;
                    out.arcs.push_back(arc);
                }
                }
            }
    }
}

// --- 2. divisione di edge e archi ---------------------------------------------------

std::vector<Interval> BooleanBuilder::splitAtPoints(const Curve<3> &curve, const Interval &range) const {
    const Box box = curveBox(curve, range).padded(tolerance_);
    const Vec3 a = curve.point(range.lo), b = curve.point(range.hi);
    std::vector<double> parameters;
    for (const Vec3 &x : vertexPoints_) {
        bool inBox = true;
        for (int i = 0; i < 3; ++i) inBox = inBox && x[i] >= box.lo[i] && x[i] <= box.hi[i];
        if (!inBox || distance(x, a) <= 2.0 * tolerance_ || distance(x, b) <= 2.0 * tolerance_) continue;
        const CurveProjection<3> projection = projectPoint(curve, x, range);
        if (projection.distance <= tolerance_) parameters.push_back(projection.parameter);
    }
    std::sort(parameters.begin(), parameters.end());
    std::vector<Interval> result;
    double start = range.lo;
    for (double t : parameters) {
        if (distance(curve.point(t), curve.point(start)) <= 2.0 * tolerance_ || distance(curve.point(t), b) <= 2.0 * tolerance_) continue;
        result.push_back({start, t});
        start = t;
    }
    result.push_back({start, range.hi});
    return result;
}

// --- 3. divisione delle facce -----------------------------------------------------

void BooleanBuilder::finishCycle(const Surface &surface, bool sense, Cycle &cycle, int samples) const {
    const double period = surface.isUPeriodic() ? surface.uPeriod() : 0.0;
    const double periodV = surface.isVPeriodic() ? surface.vPeriod() : 0.0;
    std::vector<Vec2> &polygon = cycle.polygon;
    if (isPlane(surface)) {
        const Frame3 &frame = static_cast<const Plane &>(surface).frame();
        for (const Piece &piece : cycle.pieces)
            for (int j = 0; j < samples; ++j) {
                const Vec3 local = frame.toLocal(piece.pointAt(double(j) / samples));
                polygon.push_back(Vec2(local.x(), local.y()));
            }
        polygon.push_back(polygon.front());
    } else {
        // Ogni tratto dal suo punto medio (proiezione) verso gli estremi, poi
        // spostato di periodi interi per attaccarsi al precedente. In un polo
        // u non e' definito: vale il limite lungo il tratto, e tra un tratto
        // che arriva nel polo e il successivo che ne riparte il contorno
        // cammina lungo la linea del polo, nel verso che lascia il dominio a sinistra.
        const std::vector<SurfacePole> poles = surfacePoles(surface);
        const double acceptOnSurface = 10.0 * tolerance_ + 1e-9 * scale_;
        Vec2 start, end;
        double startNear = 0.0, endNear = 0.0;  // u dei campioni vicini agli estremi
        int startPole = -1, endPole = -1;
        bool endTop = false;
        auto walk = [&](const Vec2 &from, double to, double v) {
            for (int i = 0; i < 8; ++i) polygon.push_back(Vec2(from[0] + (to - from[0]) * i / 8.0, v));
        };
        for (std::size_t p = 0; p < cycle.pieces.size(); ++p) {
            const Piece &piece = cycle.pieces[p];
            // Edge tolleranti dei file; gli estremi presi su un edge tollerante
            // possono cadere appena oltre il bordo della superficie.
            const double accept = std::max({acceptOnSurface, 1.01 * piece.tolerance, 10.0 * std::max(piece.loTolerance, piece.hiTolerance)});
            std::vector<Vec2> uvs(samples + 1);
            const int middle = samples / 2;
            const SurfaceProjection projection = projectPoint(surface, piece.pointAt(0.5));
            uvs[middle] = Vec2(projection.u, projection.v);
            for (int j = middle + 1; j <= samples; ++j) {
                uvs[j] = uvs[j - 1];
                if (!invertPoint(surface, piece.pointAt(double(j) / samples), uvs[j], accept, scale_))
                    throw std::domain_error("booleanOperation: bordo di una faccia fuori dalla superficie");
            }
            for (int j = middle; j-- > 0;) {
                uvs[j] = uvs[j + 1];
                if (!invertPoint(surface, piece.pointAt(double(j) / samples), uvs[j], accept, scale_))
                    throw std::domain_error("booleanOperation: bordo di una faccia fuori dalla superficie");
            }
            const int poleStart = poleIndex(poles, piece.start(), tolerance_), poleEnd = poleIndex(poles, piece.end(), tolerance_);
            if (poleStart >= 0) uvs[0] = Vec2(2.0 * uvs[1][0] - uvs[2][0], poles[poleStart].v);
            if (poleEnd >= 0) uvs[samples] = Vec2(2.0 * uvs[samples - 1][0] - uvs[samples - 2][0], poles[poleEnd].v);
            Vec2 shift;
            if (p == 0) {
                start = uvs[0];
                startNear = uvs[1][0];
                startPole = poleStart;
            } else {
                for (int d = 0; d < 2; ++d) {
                    const double periodD = d == 0 ? period : periodV;
                    if (periodD > 0.0) shift[d] = periodD * std::round((end[d] - uvs[0][d]) / periodD);
                }
                if (endPole >= 0 && endPole == poleStart && period > 0.0) {
                    const double target = poleWalk(end[0], uvs[0][0], period, endTop, sense, endNear, uvs[1][0]);
                    shift[0] = target - uvs[0][0];
                    walk(end, target, poles[endPole].v);
                }
            }
            for (int j = 0; j < samples; ++j) polygon.push_back(uvs[j] + shift);
            end = uvs[samples] + shift;
            endNear = uvs[samples - 1][0] + shift[0];
            endPole = poleEnd;
            endTop = poleEnd >= 0 && uvs[samples - 1][1] < poles[poleEnd].v;
        }
        // Chiusura (anche questa puo' passare per un polo).
        Vec2 closing = end;
        if (endPole >= 0 && endPole == startPole && period > 0.0) {
            closing[0] = poleWalk(end[0], start[0], period, endTop, sense, endNear, startNear);
            walk(end, closing[0], poles[endPole].v);
        }
        polygon.push_back(closing);
    }
    const double du = polygon.back()[0] - polygon.front()[0], dv = polygon.back()[1] - polygon.front()[1];
    polygon.pop_back();
    cycle.wrap = period > 0.0 ? int(std::lround(du / period)) : 0;
    cycle.wrapV = periodV > 0.0 ? int(std::lround(dv / periodV)) : 0;
    double area = 0.0, vdu = 0.0, udv = 0.0;
    for (std::size_t i = 0; i < polygon.size(); ++i) {
        Vec2 a = polygon[i], b = polygon[(i + 1) % polygon.size()];
        if (i + 1 == polygon.size()) b += Vec2(cycle.wrap * period, cycle.wrapV * periodV);  // chiusura di un ciclo avvolto
        area += cross(a, b);
        vdu += 0.5 * (a[1] + b[1]) * (b[0] - a[0]);
        udv += 0.5 * (a[0] + b[0]) * (b[1] - a[1]);
    }
    cycle.area = 0.5 * area * (sense ? 1.0 : -1.0);  // positiva per i contorni esterni
    cycle.meanV = cycle.wrap != 0 ? vdu / (cycle.wrap * period) : 0.0;
    cycle.meanU = cycle.wrapV != 0 ? udv / (cycle.wrapV * periodV) : 0.0;
}

// Semiretta nello spazio (u, v) contro i poligoni dei cicli (verticale, o
// orizzontale se la verticale non ne incontra: i cicli avvolti in v del toro
// sono linee verticali): il primo attraversamento sopra (o sotto) dice da
// che parte sta il dominio. Nelle direzioni periodiche la semiretta fa il
// giro. Nessun attraversamento: dentro solo se il dominio non ha contorno esterno.
bool rayInside(const std::vector<const Cycle *> &cycles, const Vec2 &uv, const double periods[2], double sign, bool whole) {
    // La semiretta orizzontale serve solo ai cicli avvolti in v (verticali).
    bool vertical = false;
    for (const Cycle *cycle : cycles) vertical = vertical || cycle->wrapV != 0;
    for (int along : {1, 0}) {  // along: coordinata che varia lungo la semiretta
        if (along == 0 && !vertical) break;
        const int across = 1 - along;
        const double period = periods[across], periodAlong = periods[along];
        double above = 1e300, below = -1e300;
        int aboveOrientation = 0, belowOrientation = 0;
        for (const Cycle *cycle : cycles) {
            const std::vector<Vec2> &polygon = cycle->polygon;
            for (std::size_t i = 0; i < polygon.size(); ++i) {
                Vec2 a = polygon[i], b = polygon[(i + 1) % polygon.size()];
                if (i + 1 == polygon.size()) b += Vec2(cycle->wrap * periods[0], cycle->wrapV * periods[1]);
                if (a[across] == b[across]) continue;
                const double lo = std::min(a[across], b[across]), hi = std::max(a[across], b[across]);
                int kLo = 0, kHi = 0;
                if (period > 0.0) {
                    kLo = int(std::floor((lo - uv[across]) / period)) - 1;
                    kHi = int(std::ceil((hi - uv[across]) / period)) + 1;
                }
                for (int k = kLo; k <= kHi; ++k) {
                    const double c = uv[across] + k * period;
                    if (!(c > lo && c <= hi)) continue;
                    double value = a[along] + (b[along] - a[along]) * (c - a[across]) / (b[across] - a[across]);
                    // Verso del dominio: con la semiretta verticale, un tratto che
                    // avanza in u ha il dominio sopra; con quella orizzontale, un
                    // tratto che avanza in v ha il dominio a sinistra (u minori).
                    const int orientation = (b[across] > a[across] ? 1 : -1) * int(sign) * (along == 1 ? 1 : -1);
                    // Direzione periodica lungo la semiretta: il valore piu' vicino sopra e sotto.
                    if (periodAlong > 0.0) {
                        double up = value + periodAlong * std::ceil((uv[along] - value) / periodAlong);
                        if (up <= uv[along]) up += periodAlong;
                        const double down = up - periodAlong;
                        if (up < above) {
                            above = up;
                            aboveOrientation = orientation;
                        }
                        if (down > below) {
                            below = down;
                            belowOrientation = orientation;
                        }
                        continue;
                    }
                    if (value > uv[along] && value < above) {
                        above = value;
                        aboveOrientation = orientation;
                    } else if (value <= uv[along] && value > below) {
                        below = value;
                        belowOrientation = orientation;
                    }
                }
            }
        }
        if (aboveOrientation != 0) return aboveOrientation < 0;
        if (belowOrientation != 0) return belowOrientation > 0;
    }
    return whole;
}

bool BooleanBuilder::insideSubFace(const SubFace &subFace, const Vec2 &uv) const {
    const Face &face = bodies_[subFace.body].face(subFace.face);
    std::vector<const Cycle *> cycles;
    for (const Cycle &cycle : subFace.cycles) cycles.push_back(&cycle);
    const double periods[2] = {face.surface->isUPeriodic() ? face.surface->uPeriod() : 0.0,
                               face.surface->isVPeriodic() ? face.surface->vPeriod() : 0.0};
    return rayInside(cycles, uv, periods, face.sense ? 1.0 : -1.0, subFace.whole);
}

// La superficie degenera in un punto (polo della sfera, vertice del cono,
// estremo del meridiano sull'asse) alla quota v?
bool poleAt(const Surface &surface, double v) {
    try {
        for (double u : {0.3, 2.1, 4.4}) {
            Vec3 d[4];
            surface.evaluate(u, v, 1, d);
            if (norm(d[Surface::derivativeIndex(1, 0, 1)]) > 1e-9 * (1.0 + norm(d[0]))) return false;
        }
        return true;
    } catch (const std::exception &) {
        return false;
    }
}

// I cicli si confrontano nello spazio (u, v) come poligoni: se sono troppo
// grossolani rispetto alla distanza tra due cicli (una corona sottile attorno
// a un cerchio grande: la freccia delle corde supera la larghezza) la
// divisione non riesce, e si ripete con poligoni via via piu' fitti.
std::vector<SubFace> BooleanBuilder::buildSubFaces(int k, FaceId f, const std::vector<Piece> &cuts) const {
    for (int samples : {24, 96, 384, 1536}) {
        try {
            return buildSubFacesOnce(k, f, cuts, samples);
        } catch (const std::domain_error &) {
            if (samples == 1536) throw;
        }
    }
    return {};
}

std::vector<SubFace> BooleanBuilder::buildSubFacesOnce(int k, FaceId f, const std::vector<Piece> &cuts, int polygonSamples) const {
    const Body &body = bodies_[k];
    const Face &face = body.face(f);
    const Surface &surface = *face.surface;

    // Semilati: pezzi di bordo nel verso della fin, tagli nei due versi.
    std::vector<Piece> halfEdges;
    for (LoopId l : face.loops)
        for (FinId finId : body.loopFins(l)) {
            const Fin &fin = body.fin(finId);
            const Edge &edge = body.edge(fin.edge);
            // Tolleranza degli estremi: del vertice, dell'edge (checkBody accetta
            // lo scarto curva-vertice entro la piu' grande) e lo scarto effettivo.
            const Vertex &startVertex = body.vertex(body.edgeStart(fin.edge)), &endVertex = body.vertex(body.edgeEnd(fin.edge));
            const double startTolerance = std::max({startVertex.tolerance, edge.tolerance, 1.01 * distance(startVertex.point, edge.curve->point(edge.range.lo))});
            const double endTolerance = std::max({endVertex.tolerance, edge.tolerance, 1.01 * distance(endVertex.point, edge.curve->point(edge.range.hi))});
            for (const Interval &range : splitAtPoints(*edge.curve, edge.range)) {
                Piece piece;
                piece.curve = edge.curve;
                piece.range = range;
                piece.forward = fin.sense;
                piece.tolerance = edge.tolerance;
                if (range.lo == edge.range.lo && startTolerance > tolerance_) {
                    piece.loVertex = startVertex.point;
                    piece.loTolerance = startTolerance;
                }
                if (range.hi == edge.range.hi && endTolerance > tolerance_) {
                    piece.hiVertex = endVertex.point;
                    piece.hiTolerance = endTolerance;
                }
                halfEdges.push_back(piece);
            }
        }
    for (const Piece &cut : cuts) {
        halfEdges.push_back(cut);
        halfEdges.push_back(cut.reversed());
    }

    std::vector<Vec3> nodes;
    std::vector<double> nodeTolerances;
    auto nodeOf = [&](const Vec3 &p, double tolerance) {
        for (std::size_t i = 0; i < nodes.size(); ++i)
            if (distance(nodes[i], p) <= std::max({tolerance_, tolerance, nodeTolerances[i]})) {
                if (tolerance > nodeTolerances[i]) {  // un vertice tollerante fa da nodo
                    nodes[i] = p;
                    nodeTolerances[i] = tolerance;
                }
                return int(i);
            }
        nodes.push_back(p);
        nodeTolerances.push_back(tolerance);
        return int(nodes.size()) - 1;
    };
    std::vector<int> from, to;
    {
        // Tagli che finiscono nel nodo da cui partono e sono lunghi al piu'
        // quanto la sua tolleranza (tra un punto su un edge tollerante e il
        // punto della curva accanto): non tagliano nulla.
        // Prima i nodi tolleranti, dal piu' tollerante: un punto entro la
        // tolleranza di un vertice vi si unisce anche se viene prima nell'elenco.
        std::vector<std::pair<double, Vec3>> tolerant;
        for (const Piece &piece : halfEdges) {
            if (piece.startTolerance() > tolerance_) tolerant.emplace_back(piece.startTolerance(), piece.startNode());
            if (piece.endTolerance() > tolerance_) tolerant.emplace_back(piece.endTolerance(), piece.endNode());
        }
        std::stable_sort(tolerant.begin(), tolerant.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
        for (const auto &[tolerance, point] : tolerant) nodeOf(point, tolerance);
        // Anche i pezzi di bordo cosi' corti (un edge che il piano taglia
        // entro la tolleranza del vertice) spariscono nel nodo.
        std::vector<Piece> kept;
        for (const Piece &piece : halfEdges) {
            const int a = nodeOf(piece.startNode(), piece.startTolerance()), b = nodeOf(piece.endNode(), piece.endTolerance());
            if (a == b && nodeTolerances[std::size_t(a)] > tolerance_
                && distance(piece.start(), piece.middle()) <= 2.0 * std::max({tolerance_, nodeTolerances[std::size_t(a)]}))
                continue;
            kept.push_back(piece);
            from.push_back(a);
            to.push_back(b);
        }
        halfEdges = std::move(kept);
    }
    const std::size_t count = halfEdges.size();
    std::vector<std::vector<int>> outgoing(nodes.size());
    for (std::size_t h = 0; h < count; ++h) outgoing[from[h]].push_back(int(h));

    // Angoli attorno alla normale della faccia in ogni nodo.
    std::vector<Vec3> normals(nodes.size()), references(nodes.size());
    std::vector<double> startAngle(count);
    auto project = [](const Vec3 &d, const Vec3 &n) { return d - dot(d, n) * n; };
    auto angleOf = [&](int node, const Vec3 &direction) {
        const Vec3 d = project(direction, normals[node]);
        return std::atan2(dot(cross(references[node], d), normals[node]), dot(references[node], d));
    };
    std::vector<bool> useChords(nodes.size(), false);
    for (std::size_t v = 0; v < nodes.size(); ++v) {
        if (outgoing[v].empty()) continue;
        const Vec3 toward = halfEdges[outgoing[v].front()].pointAt(0.01);
        normals[v] = faceNormal(surface, face.sense, nodes[v], tolerance_, &toward);
        references[v] = normalized(project(halfEdges[outgoing[v].front()].startTangent(), normals[v]));
        // Direzioni uscenti e (girate) entranti dal bordo: se due coincidono (tratti
        // tangenti nel nodo) l'ordine si decide con le corde.
        std::vector<double> angles;
        for (int h : outgoing[v]) angles.push_back(angleOf(int(v), halfEdges[h].startTangent()));
        for (std::size_t h = 0; h < count; ++h)  // i tagli entranti hanno il gemello tra gli uscenti
            if (to[h] == int(v) && !halfEdges[h].cut) angles.push_back(angleOf(int(v), -halfEdges[h].endTangent()));
        for (std::size_t i = 0; i < angles.size(); ++i)
            for (std::size_t j = i + 1; j < angles.size(); ++j) {
                const double gap = std::fabs(std::remainder(angles[i] - angles[j], kTwoPi));
                if (gap < 1e-6) useChords[v] = true;  // anche le tangenze approssimate delle curve tracciate
            }
    }
    // Direzione di partenza: la tangente, oppure (tangenze) la corda verso un
    // punto del tratto a una distanza fissa dal nodo, la stessa per tutti i
    // tratti del nodo (a una frazione fissa della lunghezza due cerchi
    // tangenti di raggio diverso darebbero la stessa corda).
    std::vector<double> lengths(count), reach(nodes.size(), 1e300), shortestAt(nodes.size(), 1e300);
    for (std::size_t h = 0; h < count; ++h) {
        const Piece &piece = halfEdges[h];
        lengths[h] = distance(piece.start(), piece.pointAt(0.25)) + distance(piece.pointAt(0.25), piece.middle())
                   + distance(piece.middle(), piece.pointAt(0.75)) + distance(piece.pointAt(0.75), piece.end());
        reach[from[h]] = std::min(reach[from[h]], 1e-3 * lengths[h]);
        reach[to[h]] = std::min(reach[to[h]], 1e-3 * lengths[h]);
        shortestAt[from[h]] = std::min(shortestAt[from[h]], lengths[h]);
        shortestAt[to[h]] = std::min(shortestAt[to[h]], lengths[h]);
    }
    // Nodi tolleranti (vertici dei file, punti presi su edge tolleranti): vicino
    // al nodo le curve possono passare dall'altra parte del bordo entro la
    // tolleranza e le tangenti ingannano; l'ordine si decide con le corde verso
    // punti ben oltre la tolleranza.
    for (std::size_t v = 0; v < nodes.size(); ++v)
        if (nodeTolerances[v] > tolerance_ && shortestAt[v] < 1e300) {
            useChords[v] = true;
            reach[v] = std::min(0.25 * shortestAt[v], std::max(reach[v], 20.0 * nodeTolerances[v]));
        }
    auto startDirection = [&](int h) {
        const Piece &piece = halfEdges[h];
        if (!useChords[from[h]]) return piece.startTangent();
        return piece.pointAt(reach[from[h]] / lengths[h]) - piece.start();
    };
    auto endDirection = [&](int h) {
        const Piece &piece = halfEdges[h];
        if (!useChords[to[h]]) return -piece.endTangent();
        return piece.pointAt(1.0 - reach[to[h]] / lengths[h]) - piece.end();
    };
    for (std::size_t h = 0; h < count; ++h) startAngle[h] = angleOf(from[h], startDirection(int(h)));

    auto next = [&](int h) {
        const int v = to[h];
        const double reverse = angleOf(v, endDirection(h));
        int best = -1;
        double bestRotation = 1e300;
        for (int o : outgoing[v]) {
            double rotation = std::fmod(reverse - startAngle[o], kTwoPi);
            if (rotation < 0.0) rotation += kTwoPi;
            // Tornare indietro (lo stesso tratto nell'altro verso): solo se non
            // c'e' altro. Con le corde i due angoli possono differire di un arrotondamento.
            const bool twin = halfEdges[o].curve == halfEdges[h].curve && halfEdges[o].range.lo == halfEdges[h].range.lo
                           && halfEdges[o].range.hi == halfEdges[h].range.hi && halfEdges[o].forward != halfEdges[h].forward;
            if (rotation <= 1e-12 || rotation >= kTwoPi - 1e-12 || twin) rotation = kTwoPi;
            if (rotation < bestRotation) {
                bestRotation = rotation;
                best = o;
            }
        }
        return best;
    };

    std::vector<Cycle> cycles;
    std::vector<bool> used(count, false);
    // Un ciclo che passa due volte per lo stesso nodo (due fori o due pezzi
    // che si toccano in un punto) si divide in due cicli.
    // Sul toro pero' un contorno che si tocca in un nodo e si scompone in
    // due cicli avvolti nelle due direzioni (i cerchi di Villarceau di un
    // piano bitangente) delimita un disco "pizzicato": resta un ciclo solo.
    const bool doublyPeriodic = surface.isUPeriodic() && surface.isVPeriodic();
    auto windsBothWays = [&](const std::vector<int> &sequence) {
        Cycle probe;
        for (int h : sequence) probe.pieces.push_back(halfEdges[h]);
        finishCycle(surface, face.sense, probe, polygonSamples);
        return probe.wrap != 0 && probe.wrapV != 0;
    };
    // Nei poli un contorno che ci ripassa (i rami di un contatto di ordine
    // superiore nel polo, tangenti tra loro) non si divide: nello spazio
    // (u, v) i suoi passaggi sono tratti distinti della linea del polo.
    const std::vector<SurfacePole> facePoles = surfacePoles(surface);
    std::function<void(const std::vector<int> &)> addCycle = [&](const std::vector<int> &sequence) {
        for (std::size_t i = 0; i < sequence.size(); ++i)
            for (std::size_t j = i + 1; j < sequence.size(); ++j)
                if (from[sequence[i]] == from[sequence[j]] && poleIndex(facePoles, nodes[from[sequence[i]]], tolerance_) < 0) {
                    std::vector<int> inner(sequence.begin() + i, sequence.begin() + j), outer(sequence.begin(), sequence.begin() + i);
                    outer.insert(outer.end(), sequence.begin() + j, sequence.end());
                    if (doublyPeriodic && (windsBothWays(inner) || windsBothWays(outer))) continue;
                    addCycle(inner);
                    addCycle(outer);
                    return;
                }
        // Taglio isolato dentro la faccia (percorso avanti e indietro, per
        // esempio dove l'altro solido la tocca lungo una retta): non divide nulla.
        bool slit = true;
        for (int h : sequence) {
            bool twin = false;
            for (int g : sequence)
                twin = twin || (g != h && halfEdges[h].cut && halfEdges[g].cut && distance(halfEdges[h].start(), halfEdges[g].end()) <= tolerance_
                                && distance(halfEdges[h].end(), halfEdges[g].start()) <= tolerance_
                                && distance(halfEdges[h].middle(), halfEdges[g].middle()) <= 10.0 * tolerance_);
            slit = slit && twin;
        }
        if (slit) return;
        Cycle cycle;
        for (int h : sequence) cycle.pieces.push_back(halfEdges[h]);
        finishCycle(surface, face.sense, cycle, polygonSamples);
        cycles.push_back(std::move(cycle));
    };
    for (std::size_t start = 0; start < count; ++start) {
        if (used[start]) continue;
        std::vector<int> sequence;
        int h = int(start);
        for (std::size_t guard = 0;; ++guard) {
            if (guard > count || h < 0 || used[h]) {
                throw std::domain_error("booleanOperation: divisione di una faccia non riuscita");
            }
            used[h] = true;
            sequence.push_back(h);
            h = next(h);
            if (h == int(start)) break;
        }
        addCycle(sequence);
    }

    // Raggruppamento: contorni esterni con i loro fori; sulle superfici
    // periodiche anche fasce tra due cicli avvolti (o tra un ciclo e un polo,
    // o attraverso il taglio di una direzione periodica); sulle superfici
    // chiuse anche "tutto meno i fori".
    const double periods[2] = {surface.isUPeriodic() ? surface.uPeriod() : 0.0, surface.isVPeriodic() ? surface.vPeriod() : 0.0};
    const double sign = face.sense ? 1.0 : -1.0;
    std::vector<int> outers, holes, wrapU, wrapV;
    for (std::size_t i = 0; i < cycles.size(); ++i) {
        const Cycle &cycle = cycles[i];
        if (std::abs(cycle.wrap) > 1 || std::abs(cycle.wrapV) > 1) throw std::domain_error("booleanOperation: ciclo avvolto piu' volte");
        if (cycle.wrap != 0 && cycle.wrapV != 0) throw std::domain_error("booleanOperation: ciclo avvolto nelle due direzioni del toro non gestito");
        if (cycle.wrap != 0) wrapU.push_back(int(i));
        else if (cycle.wrapV != 0) wrapV.push_back(int(i));
        else (cycle.area > 0.0 ? outers : holes).push_back(int(i));
    }
    std::vector<SubFace> result;
    std::vector<std::vector<int>> members;
    std::vector<bool> wholeMember;
    for (int o : outers) {
        members.push_back({o});
        wholeMember.push_back(false);
    }
    const std::size_t bandStart = members.size();
    // Superficie chiusa: dove non ci sono contorni resta tutta la superficie.
    const Interval vDomain = surface.vDomain();
    const bool poleLow = vDomain.isFinite() && poleAt(surface, vDomain.lo), poleHigh = vDomain.isFinite() && poleAt(surface, vDomain.hi);
    const bool closedSurface = periods[0] > 0.0 && (periods[1] > 0.0 || (poleLow && poleHigh));
    // Fasce: `list` ordinata lungo la direzione trasversale; `side` +1 se il
    // dominio sta verso le quote maggiori, -1 verso le minori.
    auto bands = [&](std::vector<int> list, const std::function<double(int)> &level, const std::function<int(int)> &side, bool cyclic,
                     bool closedBelow, bool closedAbove) {
        std::sort(list.begin(), list.end(), [&](int a, int b) {
            const double gap = level(a) - level(b);
            if (std::fabs(gap) > 1e-9 * (1.0 + std::fabs(level(a)))) return gap < 0.0;
            return side(a) < side(b);  // a pari quota prima chi chiude la fascia inferiore
        });
        std::size_t start = 0, end = list.size();
        if (!list.empty() && side(list.front()) < 0) {
            // Dominio sotto il primo ciclo: fino al polo, o (direzione periodica) oltre il taglio fino all'ultimo.
            if (cyclic && side(list.back()) > 0) {
                members.push_back({list.back(), list.front()});
                wholeMember.push_back(false);
                --end;
            } else if (closedBelow) {
                members.push_back({list.front()});
                wholeMember.push_back(false);
            } else {
                throw std::domain_error("booleanOperation: fascia non chiusa su una superficie periodica");
            }
            ++start;
        }
        if (end > start && side(list[end - 1]) > 0) {
            if (!closedAbove) throw std::domain_error("booleanOperation: fascia non chiusa su una superficie periodica");
            members.push_back({list[end - 1]});
            wholeMember.push_back(false);
            --end;
        }
        if ((end - start) % 2) throw std::domain_error("booleanOperation: cicli avvolti spaiati");
        for (std::size_t i = start; i < end; i += 2) {
            if (side(list[i]) != 1 || side(list[i + 1]) != -1) throw std::domain_error("booleanOperation: fascia non chiusa su una superficie periodica");
            members.push_back({list[i], list[i + 1]});
            wholeMember.push_back(false);
        }
    };
    if (!wrapU.empty()) {
        // Poli: vertice del cono sopra o sotto i cicli, estremi del dominio in v.
        bool below = poleLow, above = poleHigh;
        if (surface.type() == SurfaceType::Cone) {
            const auto &cone = static_cast<const ConicalSurface &>(surface);
            const double apex = -cone.referenceRadius() / std::sin(cone.semiAngle());
            double lowest = 1e300, highest = -1e300;
            for (int c : wrapU) {
                lowest = std::min(lowest, cycles[c].meanV);
                highest = std::max(highest, cycles[c].meanV);
            }
            below = apex < lowest;
            above = apex > highest;
        }
        bands(wrapU, [&](int c) { return cycles[c].meanV; }, [&](int c) { return int(sign) * cycles[c].wrap; }, periods[1] > 0.0, below, above);
    }
    if (!wrapV.empty())
        bands(wrapV, [&](int c) { return cycles[c].meanU; }, [&](int c) { return -int(sign) * cycles[c].wrapV; }, periods[0] > 0.0, false, false);
    int implicitWhole = -1;
    auto wholeOne = [&]() {
        if (implicitWhole < 0) {
            if (!closedSurface) throw std::domain_error("booleanOperation: foro fuori da ogni contorno");
            members.push_back({});
            wholeMember.push_back(true);
            implicitWhole = int(members.size()) - 1;
        }
        return implicitWhole;
    };
    for (int h : holes) {
        // Punto appena dentro il dominio del foro (a sinistra del ciclo
        // nell'orientamento della faccia): un punto sul foro stesso starebbe
        // anche sul bordo di un eventuale disco con lo stesso contorno.
        // Il lato scelto e' il piu' lontano dagli altri cicli (dove il foro
        // tocca un altro ciclo i poligoni sono troppo grossolani).
        const std::vector<Vec2> &polygon = cycles[h].polygon;
        std::size_t longest = 0;
        double bestClearance = -1.0;
        for (std::size_t i = 0; i < polygon.size(); ++i) {
            const Vec2 a = polygon[i], b = polygon[(i + 1) % polygon.size()];
            if (!(distance(a, b) > 0.0)) continue;
            const Vec2 middle = 0.5 * (a + b);
            double clearance = 1e300;
            for (std::size_t c = 0; c < cycles.size(); ++c) {
                if (int(c) == h) continue;
                for (const Vec2 &q : cycles[c].polygon)
                    for (int su = -1; su <= 1; ++su)
                        for (int sv = -1; sv <= 1; ++sv)
                            if ((su == 0 || periods[0] > 0.0) && (sv == 0 || periods[1] > 0.0))
                                clearance = std::min(clearance, distance(middle, q + Vec2(su * periods[0], sv * periods[1])));
            }
            if (clearance > bestClearance) {
                bestClearance = clearance;
                longest = i;
            }
        }
        const Vec2 a = polygon[longest], b = polygon[(longest + 1) % polygon.size()];
        const Vec2 probe = 0.5 * (a + b) + (1e-3 * sign) * perpendicular(b - a);
        int best = -1;
        double bestArea = 1e300;
        for (std::size_t m = 0; m < members.size(); ++m) {
            if (wholeMember[m]) continue;
            std::vector<const Cycle *> boundary;
            for (int c : members[m]) boundary.push_back(&cycles[c]);
            // Il punto del foro deve stare nella regione delimitata dal solo contorno.
            if (!rayInside(boundary, probe, periods, sign, false)) continue;
            const double size = m < bandStart ? cycles[members[m][0]].area : 1e299;
            if (size < bestArea) {
                bestArea = size;
                best = int(m);
            }
        }
        if (best < 0) best = wholeOne();
        members[best].push_back(h);
    }
    // Nessun ciclo (faccia senza bordo e senza tagli): la superficie intera.
    if (cycles.empty()) {
        if (!closedSurface) throw std::domain_error("booleanOperation: faccia senza bordo su una superficie aperta");
        wholeOne();
    }
    for (std::size_t m = 0; m < members.size(); ++m) {
        SubFace subFace;
        subFace.body = k;
        subFace.face = f;
        subFace.whole = wholeMember[m];
        for (int c : members[m]) subFace.cycles.push_back(cycles[c]);
        result.push_back(std::move(subFace));
    }
    return result;
}

// --- 4. classificazione --------------------------------------------------------------

Location BooleanBuilder::classify(const SubFace &subFace, const SolidClassifier &other) const {
    const Body &body = bodies_[subFace.body], &otherBody = bodies_[1 - subFace.body];
    const Face &face = body.face(subFace.face);
    const Surface &surface = *face.surface;
    auto inward = [&](const Piece &piece, Vec3 &x) {
        const double t = piece.middleParameter();
        x = piece.curve->point(t);
        const Vec3 n = faceNormal(surface, face.sense, x);
        return normalized(cross(n, normalized(piece.tangentAt(t))));
    };

    // Facce dell'altro body sulla stessa superficie: prima di tutto si guarda
    // se il pezzo ci sta sopra (le regole locali darebbero dentro/fuori a caso).
    std::vector<FaceId> coplanar;
    for (FaceId g : otherBody.faces())
        if (coincident(surface, *otherBody.face(g).surface) || partial_.count({&surface, otherBody.face(g).surface.get()}))
            coplanar.push_back(g);

    // Punti interni vicino ai tratti del bordo, dai piu' lunghi, a varie
    // frazioni del tratto: se uno cade su un contatto con l'altro solido (una
    // retta di tangenza, un bordo complanare) si prova il successivo.
    std::vector<std::pair<double, const Piece *>> pieces;
    for (const Cycle &cycle : subFace.cycles)
        for (const Piece &piece : cycle.pieces)
            pieces.emplace_back(distance(piece.start(), piece.middle()) + distance(piece.middle(), piece.end()), &piece);
    std::sort(pieces.begin(), pieces.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
    if (pieces.size() > 4) pieces.resize(4);
    auto interiorPoints = [&](const std::function<bool(const Vec3 &)> &accept) {
        if (pieces.empty()) {
            // Superficie intera senza bordo: punti qualsiasi della superficie.
            const Interval u = surface.uDomain(), v = surface.vDomain();
            for (double a : {0.13, 0.51, 0.77, 0.29})
                for (double b : {0.37, 0.61, 0.19}) {
                    const Vec3 x = surface.point(u.lo + a * u.length(), v.lo + b * v.length());
                    if (accept(x)) return true;
                }
            return false;
        }
        for (const auto &[length, piece] : pieces)
            for (double fraction : {0.5, 0.31, 0.69, 0.17, 0.83}) {
                const double t = piece->forward ? piece->range.lo + fraction * piece->range.length() : piece->range.hi - fraction * piece->range.length();
                const Vec3 x = piece->curve->point(t);
                const Vec3 w = normalized(cross(faceNormal(surface, face.sense, x), normalized(piece->tangentAt(t))));
                for (double epsilon = std::min(0.05 * length, 1e-2 * scale_); epsilon > 2.0 * tolerance_; epsilon *= 0.25) {
                    const SurfaceProjection projection = projectPoint(surface, x + epsilon * w);
                    Vec2 uv(projection.u, projection.v);
                    if (isPlane(surface)) {
                        const Vec3 local = static_cast<const Plane &>(surface).frame().toLocal(projection.point);
                        uv = Vec2(local.x(), local.y());
                    }
                    if (insideSubFace(subFace, uv) && accept(projection.point)) return true;
                }
            }
        return false;
    };

    if (!coplanar.empty()) {
        Location on = Location::Out;
        bool found = false;
        const bool ok = interiorPoints([&](const Vec3 &z) {
            for (FaceId g : coplanar) {
                // Coincidenza in parte: il punto deve stare anche sull'altra superficie.
                if (partial_.count({&surface, otherBody.face(g).surface.get()}) && projectPoint(*otherBody.face(g).surface, z).distance > tolerance_)
                    continue;
                const PointLocation location = classifyPointOnFace(otherBody, g, z, tolerance_);
                if (location == PointLocation::Boundary) return false;
                if (location == PointLocation::Inside) {
                    const Face &gface = otherBody.face(g);
                    const double d = dot(faceNormal(*gface.surface, gface.sense, z), faceNormal(surface, face.sense, z));
                    on = d > 0.0 ? Location::OnSame : Location::OnOpposite;
                    found = true;
                }
            }
            return true;
        });
        if (!ok) throw std::domain_error("booleanOperation: classificazione di un pezzo complanare non riuscita");
        if (found) return on;
    }

    // Regola locale: verso della faccia dell'altro body che produce il taglio.
    for (const Cycle &cycle : subFace.cycles)
        for (const Piece &piece : cycle.pieces) {
            // Solo dove la faccia che taglia passa davvero (non se il taglio e'
            // sul suo bordo: li' puo' finire, o proseguire in una faccia tangente).
            if (!piece.cut || piece.coplanar || !piece.partnerCrosses) continue;
            Vec3 at;
            const Vec3 into = inward(piece, at);
            const Face &partner = otherBody.face(piece.partner);
            const double d = dot(into, faceNormal(*partner.surface, partner.sense, at));
            if (std::fabs(d) > 1e-6) return d < 0.0 ? Location::In : Location::Out;
        }

    // Altrimenti un raggio da un punto interno.
    Location result = Location::Out;
    const bool ok = interiorPoints([&](const Vec3 &z) {
        const PointLocation location = other.classify(z);
        if (location == PointLocation::Boundary) return false;
        result = location == PointLocation::Inside ? Location::In : Location::Out;
        return true;
    });
    if (!ok) throw std::domain_error("booleanOperation: classificazione di un pezzo di faccia non riuscita");
    return result;
}

bool BooleanBuilder::keep(int k, Location location) const {
    if (bodies_[k].isSheet()) {
        // Lamina contro solido: la parte dentro (intersezione, bordo compreso) o fuori (differenza).
        if (operation_ == BooleanOperation::Intersect) return location != Location::Out;
        return location == Location::Out;
    }
    switch (operation_) {
    case BooleanOperation::Unite:
        return location == Location::Out || (k == 0 && location == Location::OnSame);
    case BooleanOperation::Intersect:
        return location == Location::In || (k == 0 && location == Location::OnSame);
    case BooleanOperation::Subtract:
        return k == 0 ? (location == Location::Out || location == Location::OnOpposite) : location == Location::In;
    }
    return false;
}

// --- 5. assemblaggio --------------------------------------------------------------------

// Un vertice in cui si toccano pezzi altrimenti separati (due solidi o due
// fori che si toccano in un punto) ha piu' "ombrelli" di facce attorno: ogni
// angolo di faccia (fin che arriva, fin che parte) si collega agli angoli
// delle facce vicine attraverso gli edge. Ogni ombrello oltre al primo prende
// un vertice suo, nello stesso punto: la topologia resta una varieta'.
void BooleanBuilder::splitVertices(std::vector<Vec3> &vertices, std::vector<Body::BuildEdge> &edges,
                                   const std::vector<Body::BuildFace> &faces) {
    auto endOf = [&](const Body::BuildFin &fin) { return fin.sense ? edges[fin.edge].end : edges[fin.edge].start; };
    struct Corner {
        int vertex, in, out;  // edge che arriva e che parte
    };
    std::vector<Corner> corners;
    for (const Body::BuildFace &face : faces)
        for (const std::vector<Body::BuildFin> &loop : face.loops)
            for (std::size_t i = 0; i < loop.size(); ++i) {
                const Body::BuildFin &in = loop[i], &out = loop[(i + 1) % loop.size()];
                corners.push_back({endOf(in), in.edge, out.edge});
            }
    std::vector<int> parent(corners.size());
    for (std::size_t i = 0; i < parent.size(); ++i) parent[i] = int(i);
    std::function<int(int)> find = [&](int i) { return parent[i] == i ? i : parent[i] = find(parent[i]); };
    // Angoli nello stesso vertice che condividono un edge: stesso ombrello.
    std::map<std::pair<int, int>, int> byVertexEdge;
    for (std::size_t i = 0; i < corners.size(); ++i)
        for (int e : {corners[i].in, corners[i].out}) {
            const auto key = std::make_pair(corners[i].vertex, e);
            const auto found = byVertexEdge.find(key);
            if (found == byVertexEdge.end()) byVertexEdge[key] = int(i);
            else parent[find(int(i))] = find(found->second);
        }
    std::map<int, std::vector<int>> umbrellas;  // vertice -> radici degli ombrelli
    for (std::size_t i = 0; i < corners.size(); ++i) {
        std::vector<int> &roots = umbrellas[corners[i].vertex];
        const int root = find(int(i));
        if (std::find(roots.begin(), roots.end(), root) == roots.end()) roots.push_back(root);
    }
    for (const auto &[vertex, roots] : umbrellas)
        for (std::size_t u = 1; u < roots.size(); ++u) {
            const int copy = int(vertices.size());
            vertices.push_back(vertices[vertex]);
            for (std::size_t i = 0; i < corners.size(); ++i) {
                if (corners[i].vertex != vertex || find(int(i)) != roots[u]) continue;
                // L'edge che arriva finisce qui, quello che parte comincia qui.
                for (int e : {corners[i].in, corners[i].out}) {
                    if (edges[e].start == vertex) edges[e].start = copy;
                    if (edges[e].end == vertex) edges[e].end = copy;
                }
            }
        }
}

// Un edge usato da piu' di due fin (due solidi che si toccano lungo uno
// spigolo, senza attraversarsi) si divide in edge distinti: attorno
// all'edge le facce si ordinano per angolo e si accoppiano quelle che
// delimitano lo stesso settore di materiale. Il risultato resta una varieta'
// (due shell che si toccano), come se i solidi fossero appena staccati.
void BooleanBuilder::pairRadially(const std::vector<Vec3> &, std::vector<Body::BuildEdge> &edges, std::vector<Vec3> &middles,
                                  std::vector<Body::BuildFace> &faces) const {
    struct Use {
        std::size_t face, loop, fin;
    };
    std::vector<std::vector<Use>> uses(edges.size());
    for (std::size_t f = 0; f < faces.size(); ++f)
        for (std::size_t l = 0; l < faces[f].loops.size(); ++l)
            for (std::size_t i = 0; i < faces[f].loops[l].size(); ++i) uses[faces[f].loops[l][i].edge].push_back({f, l, i});
    const std::size_t original = edges.size();
    for (std::size_t e = 0; e < original; ++e) {
        if (uses[e].size() <= 2) continue;
        const Body::BuildEdge edge = edges[e];
        const double tm = 0.5 * (edge.range.lo + edge.range.hi);
        const Vec3 m = edge.curve->point(tm), axis = normalized(edge.curve->derivative(tm));
        struct Radial {
            Use use;
            double angle;
            Vec3 normal, inward;
        };
        std::vector<Radial> radial;
        Vec3 reference;
        for (const Use &use : uses[e]) {
            const Body::BuildFace &face = faces[use.face];
            const bool sense = face.loops[use.loop][use.fin].sense;
            const Vec3 n = faceNormal(*face.surface, face.sense, m);
            // La faccia sta a sinistra della fin: verso l'interno = n x tangente della fin.
            Vec3 inward = cross(n, sense ? axis : -axis);
            inward = normalized(inward - dot(inward, axis) * axis);
            if (radial.empty()) reference = inward;
            radial.push_back({use, std::atan2(dot(cross(reference, inward), axis), dot(reference, inward)), n, inward});
        }
        std::sort(radial.begin(), radial.end(), [](const Radial &a, const Radial &b) { return a.angle < b.angle; });
        std::vector<std::pair<std::size_t, std::size_t>> pairs;
        for (std::size_t i = 0; i < radial.size(); ++i) {
            const std::size_t j = (i + 1) % radial.size();
            // Settore tra le due facce: e' materiale se la normale (uscente) della prima non vi punta.
            double sweep = radial[j].angle - radial[i].angle;
            if (sweep <= 0.0) sweep += kTwoPi;
            const double middleAngle = radial[i].angle + 0.5 * sweep;
            const Vec3 bisector = std::cos(middleAngle) * reference + std::sin(middleAngle) * cross(axis, reference);
            if (dot(radial[i].normal, bisector) < 0.0 && dot(radial[j].normal, bisector) < 0.0) pairs.emplace_back(i, j);
        }
        if (2 * pairs.size() != radial.size())
            throw std::domain_error("booleanOperation: spigolo condiviso da piu' facce non risolto");
        for (std::size_t p = 0; p < pairs.size(); ++p) {
            std::size_t index = e;
            if (p > 0) {
                index = edges.size();
                edges.push_back(edge);
                middles.push_back(middles[e]);
            }
            for (std::size_t k : {pairs[p].first, pairs[p].second}) {
                const Use &use = radial[k].use;
                faces[use.face].loops[use.loop][use.fin].edge = int(index);
            }
        }
    }
}

void BooleanBuilder::computeCuts(std::map<int, std::vector<Piece>> (&cuts)[2]) {
    // Coppie di facce i cui box si toccano, in parallelo (il tracciamento tra
    // B-spline puo' prendere secondi per coppia).
    std::vector<std::pair<FaceId, FaceId>> pairs;
    for (FaceId fa : bodies_[0].faces())
        for (FaceId fb : bodies_[1].faces())
            if (boxes_[0].at(fa.index).overlaps(boxes_[1].at(fb.index))) pairs.emplace_back(fa, fb);
    std::vector<PairResult> results(pairs.size());
    const auto compute = [&](std::size_t i) {
        try {
            pairArcs(pairs[i].first, pairs[i].second, results[i]);
        } catch (...) {
            results[i].error = std::current_exception();
        }
    };
    parallelFor(pairs.size(), threadCount(threads_), compute);
    // Unione nell'ordine delle coppie. Una coppia che ha usato i tagli delle
    // zone comuni delle precedenti (B-spline coincidenti in parte) senza
    // vederli, perche' calcolata in parallelo, si rifa' con quelli: il
    // risultato e' lo stesso del calcolo in sequenza.
    for (std::size_t i = 0; i < pairs.size(); ++i) {
        PairResult &result = results[i];
        if (result.usedEarlierCuts && !partialCuts_.empty()) {
            result = PairResult();
            result.earlierCuts = &partialCuts_;
            compute(i);
        }
        if (result.error) std::rethrow_exception(result.error);
        arcs_.insert(arcs_.end(), result.arcs.begin(), result.arcs.end());
        for (const auto &[curve, entry] : result.pcurves) pcurves_[curve] = entry;
        partial_.insert(result.partial.begin(), result.partial.end());
        partialCuts_.insert(partialCuts_.end(), result.partialCuts.begin(), result.partialCuts.end());
    }
    snapNearEnds();

    for (const Arc &arc : arcs_) {
        vertexPoints_.push_back(arc.loTolerance > 0.0 ? arc.loPoint : arc.curve->point(arc.range.lo));
        vertexPoints_.push_back(arc.hiTolerance > 0.0 ? arc.hiPoint : arc.curve->point(arc.range.hi));
        vertexTolerance_ = std::max({vertexTolerance_, arc.loTolerance, arc.hiTolerance});
    }

    // Tagli di ogni faccia, divisi nei punti dei vertici e senza doppioni.
    for (const Arc &arc : arcs_)
        for (int k = 0; k < 2; ++k) {
            if (!arc.cut[k]) continue;
            for (const Interval &range : splitAtPoints(*arc.curve, arc.range)) {
                // Pezzi di un arco che esce appena dalla superficie (seme su un edge
                // tollerante, fuori dal dominio della B-spline): fuori dalla faccia.
                if (vertexTolerance_ > tolerance_ && (range.lo != arc.range.lo || range.hi != arc.range.hi)
                    && projectPoint(*bodies_[k].face(arc.face[k]).surface, arc.curve->point(0.5 * (range.lo + range.hi))).distance > 10.0 * tolerance_)
                    continue;
                Piece piece;
                piece.curve = arc.curve;
                piece.range = range;
                piece.cut = true;
                piece.partner = arc.face[1 - k];
                piece.partnerCrosses = arc.cut[1 - k];
                piece.coplanar = arc.coplanar;
                if (range.lo == arc.range.lo && arc.loTolerance > 0.0) {
                    piece.loVertex = arc.loPoint;
                    piece.loTolerance = arc.loTolerance;
                }
                if (range.hi == arc.range.hi && arc.hiTolerance > 0.0) {
                    piece.hiVertex = arc.hiPoint;
                    piece.hiTolerance = arc.hiTolerance;
                }
                std::vector<Piece> &list = cuts[k][arc.face[k].index];
                bool duplicate = false;
                // Estremi uguali: entro la tolleranza, o quella dei punti presi su edge tolleranti.
                const auto near = [&](const Vec3 &a, double ta, const Vec3 &b, double tb) { return distance(a, b) <= std::max({tolerance_, ta, tb}); };
                for (Piece &existing : list) {
                    const bool sameEnds = (near(existing.startNode(), existing.startTolerance(), piece.startNode(), piece.startTolerance())
                                           && near(existing.endNode(), existing.endTolerance(), piece.endNode(), piece.endTolerance()))
                        || (near(existing.startNode(), existing.startTolerance(), piece.endNode(), piece.endTolerance())
                            && near(existing.endNode(), existing.endTolerance(), piece.startNode(), piece.startTolerance()));
                    // Stessa geometria anche con parametri diversi (un'isoparametrica
                    // esatta e la stessa linea tracciata): il punto medio dell'uno sull'altro.
                    if (sameEnds && (distance(existing.middle(), piece.middle()) <= 10.0 * tolerance_
                                     || projectPoint(*existing.curve, piece.middle(), existing.range).distance <= 10.0 * tolerance_)) {
                        duplicate = true;
                        if (existing.coplanar && !piece.coplanar) existing = piece;  // meglio un taglio trasversale
                        break;
                    }
                }
                if (!duplicate) list.push_back(piece);
            }
        }

}

// Dove tre superfici sono tangenti nello stesso punto (due raccordi che si
// incrociano sulla faccia che entrambi toccano, e la faccia stessa) le curve
// d'intersezione che vi arrivano si fermano con la sola precisione del
// secondo ordine: gli estremi distano qualche tolleranza invece di
// coincidere, e la vicinanza di superfici tangenti lascia archi cortissimi.
// Gli estremi di archi diversi entro kEndSnap volte la tolleranza diventano
// un vertice tollerante (il punto piu' centrale del gruppo, preferendo gli
// estremi delle curve esatte); gli archi piu' corti del raggio con i due
// estremi nello stesso gruppo spariscono.
void BooleanBuilder::snapNearEnds() {
    constexpr double kEndSnap = 20.0;
    const double radius = kEndSnap * tolerance_;
    const std::size_t n = arcs_.size();
    if (n < 2) return;
    struct End {
        std::size_t arc;
        bool hi;
        Vec3 point;
    };
    std::vector<End> ends;
    for (std::size_t i = 0; i < n; ++i) {
        const Arc &arc = arcs_[i];
        ends.push_back({i, false, arc.loTolerance > 0.0 ? arc.loPoint : arc.curve->point(arc.range.lo)});
        ends.push_back({i, true, arc.hiTolerance > 0.0 ? arc.hiPoint : arc.curve->point(arc.range.hi)});
    }
    std::vector<std::size_t> parent(ends.size());
    for (std::size_t i = 0; i < parent.size(); ++i) parent[i] = i;
    const std::function<std::size_t(std::size_t)> root = [&](std::size_t i) { return parent[i] == i ? i : parent[i] = root(parent[i]); };
    bool spread = false;
    for (std::size_t i = 0; i < ends.size(); ++i)
        for (std::size_t j = i + 1; j < ends.size(); ++j) {
            if (ends[i].arc == ends[j].arc) continue;
            const double d = distance(ends[i].point, ends[j].point);
            if (d > radius) continue;
            parent[root(i)] = root(j);
            if (d > tolerance_) spread = true;
        }
    if (!spread) return;  // gia' tutti entro la tolleranza: niente da fare
    std::map<std::size_t, std::vector<std::size_t>> groups;
    for (std::size_t i = 0; i < ends.size(); ++i) groups[root(i)].push_back(i);
    std::vector<bool> removed(n, false);
    for (const auto &[key, members] : groups) {
        if (members.size() < 2) continue;
        double width = 0.0;
        for (std::size_t a : members)
            for (std::size_t b : members) width = std::max(width, distance(ends[a].point, ends[b].point));
        if (width <= tolerance_) continue;
        // Archi cortissimi chiusi nel gruppo (contatti tra superfici tangenti).
        for (std::size_t a : members)
            for (std::size_t b : members)
                if (a < b && ends[a].arc == ends[b].arc) removed[ends[a].arc] = true;
        // Il punto del gruppo: tra gli estremi delle curve esatte, se ce ne sono, il piu' centrale.
        std::size_t best = members.front();
        double bestScore = std::numeric_limits<double>::infinity();
        for (std::size_t a : members) {
            if (removed[ends[a].arc]) continue;
            const CurveType type = arcs_[ends[a].arc].curve->type();
            const bool exact = type == CurveType::Line || type == CurveType::Circle || type == CurveType::Ellipse;
            double score = exact ? 0.0 : 1e3 * radius;
            for (std::size_t b : members) score += distance(ends[a].point, ends[b].point);
            if (score < bestScore) bestScore = score, best = a;
        }
        const Vec3 point = ends[best].point;
        for (std::size_t a : members) {
            const double gap = distance(point, ends[a].point);
            Arc &arc = arcs_[ends[a].arc];
            Vec3 &target = ends[a].hi ? arc.hiPoint : arc.loPoint;
            double &tolerance = ends[a].hi ? arc.hiTolerance : arc.loTolerance;
            target = point;
            tolerance = std::max({tolerance, 1.01 * gap, tolerance_}) + (gap > 0.0 ? tolerance_ : 0.0);
        }
    }
    std::vector<Arc> kept;
    for (std::size_t i = 0; i < n; ++i)
        if (!removed[i]) kept.push_back(arcs_[i]);
    arcs_ = std::move(kept);
}

Body BooleanBuilder::run() {
    // Lamine: solo con un solido, e il risultato e' la parte della lamina
    // dentro (intersezione) o fuori (differenza, lamina meno solido) del solido.
    const bool sheet[2] = {bodies_[0].isSheet(), bodies_[1].isSheet()};
    if (sheet[0] && sheet[1]) throw std::domain_error("booleanOperation: operazione tra due lamine non gestita");
    if ((sheet[0] || sheet[1]) && operation_ == BooleanOperation::Unite)
        throw std::domain_error("booleanOperation: l'unione di una lamina e di un solido non e' una varieta'");
    if (sheet[1] && operation_ == BooleanOperation::Subtract)
        throw std::domain_error("booleanOperation: sottrarre una lamina da un solido non ne cambia il volume");
    const bool sheetResult = sheet[0] || sheet[1];
    std::map<int, std::vector<Piece>> cuts[2];
    computeCuts(cuts);
    // Divisione e classificazione di ogni faccia sono indipendenti. Ogni
    // worker conserva i pezzi localmente; il merge successivo mantiene
    // l'ordine storico body/faccia e quindi una B-rep deterministica.
    struct FaceResult {
        int body = 0;
        FaceId face;
        std::vector<std::pair<SubFace, bool>> kept;  // pezzo e "da girare"
        std::exception_ptr error;
    };
    std::vector<FaceResult> faceResults;
    for (int k = 0; k < 2; ++k) {
        if (sheetResult && !sheet[k]) continue;  // del solido non resta nulla
        for (FaceId f : bodies_[k].faces()) faceResults.push_back({k, f, {}, {}});
    }
    std::unique_ptr<SolidClassifier> classifiers[2];
    for (const FaceResult &job : faceResults)
        if (!classifiers[job.body]) classifiers[job.body] = std::make_unique<SolidClassifier>(bodies_[1 - job.body], tolerance_);
    parallelFor(faceResults.size(), threadCount(threads_), [&](std::size_t i) {
        FaceResult &job = faceResults[i];
        try {
            const auto found = cuts[job.body].find(job.face.index);
            const std::vector<Piece> none;
            for (SubFace &subFace : buildSubFaces(job.body, job.face, found != cuts[job.body].end() ? found->second : none)) {
                const Location location = classify(subFace, *classifiers[job.body]);
                if (keep(job.body, location))
                    job.kept.emplace_back(std::move(subFace), operation_ == BooleanOperation::Subtract && job.body == 1);
            }
        } catch (...) {
            job.error = std::current_exception();
        }
    });
    std::vector<std::pair<SubFace, bool>> kept;
    for (FaceResult &job : faceResults) {
        if (job.error) std::rethrow_exception(job.error);
        kept.insert(kept.end(), std::make_move_iterator(job.kept.begin()), std::make_move_iterator(job.kept.end()));
    }
    if (kept.empty()) return Body();
    return assemble(kept, sheetResult);
}

std::vector<Body> BooleanBuilder::split() {
    if (!bodies_[0].isSheet()) throw std::domain_error("splitSheet: si dividono solo le lamine");
    std::map<int, std::vector<Piece>> cuts[2];
    computeCuts(cuts);
    struct FaceResult {
        FaceId face;
        std::vector<SubFace> pieces;
        std::exception_ptr error;
    };
    std::vector<FaceResult> faceResults;
    for (FaceId f : bodies_[0].faces()) faceResults.push_back({f, {}, {}});
    parallelFor(faceResults.size(), threadCount(threads_), [&](std::size_t i) {
        FaceResult &job = faceResults[i];
        try {
            const auto found = cuts[0].find(job.face.index);
            const std::vector<Piece> none;
            job.pieces = buildSubFaces(0, job.face, found != cuts[0].end() ? found->second : none);
        } catch (...) {
            job.error = std::current_exception();
        }
    });
    std::vector<SubFace> pieces;
    for (FaceResult &job : faceResults) {
        if (job.error) std::rethrow_exception(job.error);
        pieces.insert(pieces.end(), std::make_move_iterator(job.pieces.begin()), std::make_move_iterator(job.pieces.end()));
    }
    // Regioni: pezzi che si toccano lungo un tratto di bordo che non e' un taglio.
    std::vector<int> group(pieces.size());
    for (std::size_t i = 0; i < group.size(); ++i) group[i] = int(i);
    std::function<int(int)> root = [&](int i) { return group[std::size_t(i)] == i ? i : group[std::size_t(i)] = root(group[std::size_t(i)]); };
    auto same = [&](const Piece &a, const Piece &b) {
        const bool ends = (distance(a.start(), b.start()) <= tolerance_ && distance(a.end(), b.end()) <= tolerance_)
                       || (distance(a.start(), b.end()) <= tolerance_ && distance(a.end(), b.start()) <= tolerance_);
        return ends && (distance(a.middle(), b.middle()) <= 10.0 * tolerance_ || projectPoint(*a.curve, b.middle(), a.range).distance <= 10.0 * tolerance_);
    };
    for (std::size_t i = 0; i < pieces.size(); ++i)
        for (std::size_t j = i + 1; j < pieces.size(); ++j) {
            if (root(int(i)) == root(int(j))) continue;
            bool touching = false;
            for (const Cycle &ci : pieces[i].cycles)
                for (const Piece &a : ci.pieces) {
                    if (a.cut || touching) continue;
                    for (const Cycle &cj : pieces[j].cycles)
                        for (const Piece &b : cj.pieces)
                            if (!b.cut && same(a, b)) touching = true;
                }
            if (touching) group[std::size_t(root(int(i)))] = root(int(j));
        }
    std::map<int, std::vector<std::pair<SubFace, bool>>> regions;
    for (std::size_t i = 0; i < pieces.size(); ++i) regions[root(int(i))].emplace_back(pieces[i], false);
    std::vector<Body> result;
    for (const auto &[index, kept] : regions) result.push_back(assemble(kept, true));
    return result;
}

Body BooleanBuilder::assemble(const std::vector<std::pair<SubFace, bool>> &kept, bool sheetResult) {
    std::vector<Vec3> vertices;
    std::vector<double> vertexTolerances;
    auto vertexOf = [&](const Vec3 &p, double tolerance) {
        for (std::size_t i = 0; i < vertices.size(); ++i)
            if (distance(vertices[i], p) <= std::max({tolerance_, tolerance, vertexTolerances[i]})) {
                if (tolerance > vertexTolerances[i]) {
                    vertices[i] = p;
                    vertexTolerances[i] = tolerance;
                }
                return int(i);
            }
        vertices.push_back(p);
        vertexTolerances.push_back(tolerance);
        return int(vertices.size()) - 1;
    };
    {
        // Prima i vertici tolleranti, dal piu' tollerante (come i nodi delle facce).
        std::vector<std::pair<double, Vec3>> tolerant;
        for (const auto &[subFace, flip] : kept)
            for (const Cycle &cycle : subFace.cycles)
                for (const Piece &piece : cycle.pieces) {
                    if (piece.startTolerance() > tolerance_) tolerant.emplace_back(piece.startTolerance(), piece.startNode());
                    if (piece.endTolerance() > tolerance_) tolerant.emplace_back(piece.endTolerance(), piece.endNode());
                }
        std::stable_sort(tolerant.begin(), tolerant.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
        for (const auto &[tolerance, point] : tolerant) vertexOf(point, tolerance);
    }
    std::vector<Body::BuildEdge> edges;
    std::vector<Vec3> edgeMiddles;
    std::vector<Body::BuildFace> faces;
    for (const auto &[subFace, flip] : kept) {
        const Face &face = bodies_[subFace.body].face(subFace.face);
        Body::BuildFace built;
        built.surface = face.surface;
        built.sense = flip ? !face.sense : face.sense;
        for (const Cycle &cycle : subFace.cycles) {
            std::vector<Piece> pieces = cycle.pieces;
            if (flip) {
                std::reverse(pieces.begin(), pieces.end());
                for (Piece &piece : pieces) piece = piece.reversed();
            }
            // Speroni: un taglio che entra nella faccia e finisce dentro (per
            // esempio dove l'altro solido la tocca soltanto) e' percorso
            // avanti e indietro di seguito e non divide nulla.
            auto sameReversed = [&](const Piece &a, const Piece &b) {
                return distance(a.start(), b.end()) <= tolerance_ && distance(a.end(), b.start()) <= tolerance_
                    && (distance(a.middle(), b.middle()) <= 10.0 * tolerance_ || projectPoint(*a.curve, b.middle(), a.range).distance <= 10.0 * tolerance_);
            };
            for (bool removed = true; removed && pieces.size() >= 2;) {
                removed = false;
                for (std::size_t i = 0; i < pieces.size() && pieces.size() >= 2; ++i) {
                    const std::size_t j = (i + 1) % pieces.size();
                    if (!sameReversed(pieces[i], pieces[j])) continue;
                    pieces.erase(pieces.begin() + std::max(i, j));
                    pieces.erase(pieces.begin() + std::min(i, j));
                    removed = true;
                    break;
                }
            }
            if (pieces.empty()) continue;
            std::vector<Body::BuildFin> loop;
            for (const Piece &piece : pieces) {
                const int vs = vertexOf(piece.startNode(), piece.startTolerance()), ve = vertexOf(piece.endNode(), piece.endTolerance());
                const Vec3 middle = piece.middle();
                // Tratto piu' corto della tolleranza del vertice in cui comincia e finisce: sparisce.
                if (vs == ve && vertexTolerances[std::size_t(vs)] > tolerance_
                    && distance(piece.start(), middle) <= 2.0 * vertexTolerances[std::size_t(vs)])
                    continue;
                int index = -1;
                for (std::size_t e = 0; e < edges.size(); ++e) {
                    const bool sameEnds = (edges[e].start == vs && edges[e].end == ve) || (edges[e].start == ve && edges[e].end == vs);
                    if (sameEnds && (distance(edgeMiddles[e], middle) <= 10.0 * tolerance_
                                     || projectPoint(*edges[e].curve, middle, edges[e].range).distance <= 10.0 * tolerance_)) {
                        index = int(e);
                        break;
                    }
                }
                if (index < 0) {
                    Body::BuildEdge edge;
                    edge.curve = piece.curve;
                    edge.range = piece.range;
                    edge.start = piece.forward ? vs : ve;
                    edge.end = piece.forward ? ve : vs;
                    edge.tolerance = piece.tolerance;
                    edges.push_back(edge);
                    edgeMiddles.push_back(middle);
                    index = int(edges.size()) - 1;
                }
                // Estremi presi su un edge tollerante: la curva puo' finire appena
                // oltre il bordo della superficie, l'edge diventa tollerante.
                for (const auto &[t, snapped] : {std::pair<double, double>{piece.range.lo, piece.loTolerance}, {piece.range.hi, piece.hiTolerance}})
                    if (snapped > 0.0) {
                        const double gap = projectPoint(*face.surface, piece.curve->point(t)).distance;
                        if (gap > tolerance_) edges[index].tolerance = std::max(edges[index].tolerance, 1.5 * gap);
                    }
                // Verso della fin: tangente di percorrenza contro quella dell'edge nel punto medio.
                const Body::BuildEdge &edge = edges[index];
                const Vec3 edgeTangent = edge.curve->derivative(0.5 * (edge.range.lo + edge.range.hi));
                const CurveProjection<3> onPiece = projectPoint(*piece.curve, edgeMiddles[index], piece.range);
                Body::BuildFin fin;
                fin.edge = index;
                fin.sense = dot(piece.tangentAt(onPiece.parameter), edgeTangent) > 0.0;
                loop.push_back(fin);
            }
            if (!loop.empty()) built.loops.push_back(std::move(loop));
        }
        faces.push_back(std::move(built));
    }
    pairRadially(vertices, edges, edgeMiddles, faces);
    splitVertices(vertices, edges, faces);
    Body result;
    try {
        result = sheetResult ? Body::buildSheet(vertices, edges, faces) : Body::build(vertices, edges, faces);
    } catch (const std::invalid_argument &error) {
        throw std::domain_error(std::string("booleanOperation: risultato non chiuso (") + error.what() + ")");
    }
    // SP-curve approssimate delle curve tracciate (le altre le calcola computePCurves).
    for (FinId f : result.fins()) {
        Fin &fin = result.fin(f);
        const auto found = pcurves_.find(result.edge(fin.edge).curve.get());
        if (fin.pcurve || found == pcurves_.end()) continue;
        const Surface *surface = result.face(result.finFace(f)).surface.get();
        for (int k = 0; k < 2; ++k)
            if (found->second.surface[k] == surface) {
                fin.pcurve = found->second.pcurve[k];
                fin.pcurveTolerance = std::max(found->second.deviation, std::numeric_limits<double>::min());
            }
    }
    // Estremi che toccano il vertice solo entro la tolleranza della booleana
    // (punti uniti da curve diverse: un'isoparametrica esatta e una curva
    // tracciata) o dei vertici tolleranti dei body: l'edge diventa tollerante.
    auto tolerantEnds = [&](Body &body) {
        for (EdgeId e : body.edges()) {
            Edge &edge = body.edge(e);
            if (!edge.curve) continue;
            const Vec3 a = edge.curve->point(edge.range.lo), b = edge.curve->point(edge.range.hi);
            const double gap = std::max(distance(a, body.vertex(body.edgeStart(e)).point), distance(b, body.vertex(body.edgeEnd(e)).point));
            if (gap > edge.tolerance && gap <= std::max(tolerance_, vertexTolerance_)) edge.tolerance = 1.01 * gap;
        }
        // Vertici tolleranti (le curve che vi arrivano si fermano entro la loro
        // tolleranza): la tolleranza resta sul vertice per le operazioni successive.
        for (EdgeId e : body.edges()) {
            const Edge &edge = body.edge(e);
            if (!edge.curve) continue;
            for (const auto &[v, t] : {std::pair<VertexId, double>{body.edgeStart(e), edge.range.lo}, {body.edgeEnd(e), edge.range.hi}}) {
                const double gap = distance(body.vertex(v).point, edge.curve->point(t));
                if (gap > 1e-7) body.vertex(v).tolerance = std::max(body.vertex(v).tolerance, 1.01 * gap);
            }
        }
    };
    tolerantEnds(result);
    computePCurves(result);
    if (unify_) result = unifySameDomain(result, tolerance_);
    tolerantEnds(result);
    const std::vector<CheckIssue> issues = checkBody(result);
    if (!issues.empty())
        throw std::domain_error("booleanOperation: risultato non valido (" + describe(issues.front().code) + ": " + issues.front().message + ")");
    return result;
}

}

Body booleanOperation(const Body &a, const Body &b, BooleanOperation operation, const BooleanOptions &options) {
    return BooleanBuilder(a, b, operation, options).run();
}

std::vector<Body> splitSheet(const Body &sheet, const Body &tool, const BooleanOptions &options) {
    return BooleanBuilder(sheet, tool, BooleanOperation::Intersect, options, true).split();
}

}
