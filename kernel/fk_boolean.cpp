#include "fk_boolean.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>

#include "fk_body_check.h"
#include "fk_classify.h"
#include "fk_curve_algo.h"
#include "fk_intersect.h"
#include "fk_marching.h"
#include "fk_pcurve.h"
#include "fk_surface_algo.h"

namespace ForgeCad::Kernel {
namespace {

// Tratto di un arco d'intersezione che sta in entrambe le facce (o sul loro bordo).
struct Arc {
    CurvePtr<3> curve;
    Interval range;
    FaceId face[2];
    bool cut[2] = {false, false};  // divide l'interno della faccia del body 0 / 1
    bool coplanar = false;         // viene da due facce complanari
};

// Tratto di curva percorso in un verso: pezzo di bordo o taglio.
struct Piece {
    CurvePtr<3> curve;
    Interval range;
    bool forward = true;
    double tolerance = 0.0;
    bool cut = false;
    FaceId partner;  // per i tagli: la faccia dell'altro body che taglia
    bool coplanar = false;

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
    int wrap = 0;               // giri in u (superfici periodiche)
    double area = 0.0, meanV = 0.0;
};

struct SubFace {
    int body = 0;
    FaceId face;
    std::vector<Cycle> cycles;
};

bool isPlane(const Surface &s) { return s.type() == SurfaceType::Plane; }

Vec3 faceNormal(const Surface &surface, bool sense, const Vec3 &x) {
    const SurfaceProjection projection = projectPoint(surface, x);
    const Vec3 n = surface.normal(projection.u, projection.v);
    return sense ? n : -n;
}

class BooleanBuilder {
public:
    BooleanBuilder(const Body &a, const Body &b, BooleanOperation operation, double tolerance)
        : operation_(operation), tolerance_(tolerance) {
        bodies_[0] = a;
        bodies_[1] = b;
        Box all;
        for (int k = 0; k < 2; ++k) {
            computePCurves(bodies_[k]);
            for (FaceId f : bodies_[k].faces()) {
                if (!bodies_[k].face(f).surface) throw std::invalid_argument("booleanOperation: faccia senza superficie");
                boxes_[k][f.index] = faceBox(bodies_[k], f).padded(tolerance_);
                all.add(boxes_[k][f.index]);
            }
        }
        scale_ = std::max(all.diagonal(), 1.0);
    }

    Body run();

private:
    void pairArcs(FaceId fa, FaceId fb);
    void coplanarArcs(FaceId fa, FaceId fb);
    void surfaceArcs(FaceId fa, FaceId fb);
    Box commonBounds(FaceId fa, FaceId fb) const;
    bool touchesBoth(FaceId fa, FaceId fb, const Vec3 &x) const;
    void rejectTangentLines(FaceId fa, FaceId fb, const std::vector<CurvePtr<3>> &curves, const std::vector<Interval> &ranges) const;
    void addArcs(const CurvePtr<3> &curve, const Interval &range, const std::vector<double> &parameters, FaceId fa, FaceId fb,
                 const IntersectionCurve *source = nullptr);
    std::vector<Interval> splitRange(const Curve<3> &curve, const Interval &range, std::vector<double> parameters) const;
    std::vector<Interval> splitAtPoints(const Curve<3> &curve, const Interval &range) const;
    std::vector<SubFace> buildSubFaces(int k, FaceId f, const std::vector<Piece> &cuts) const;
    void finishCycle(const Surface &surface, bool sense, Cycle &cycle) const;
    bool insideSubFace(const SubFace &subFace, const Vec2 &uv) const;
    Location classify(const SubFace &subFace, const SolidClassifier &other) const;
    bool keep(int k, Location location) const;

    Body bodies_[2];
    BooleanOperation operation_;
    double tolerance_, scale_ = 1.0;
    std::map<int, Box> boxes_[2];
    std::vector<Arc> arcs_;
    std::vector<Vec3> vertexPoints_;
    // SP-curve delle curve d'intersezione approssimate, per curva e superficie.
    struct PCurves {
        const Surface *surface[2];
        CurvePtr<2> pcurve[2];
        double deviation;
    };
    std::map<const Curve<3> *, PCurves> pcurves_;
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
    std::vector<double> unique;
    for (double t : parameters)
        if (unique.empty() || distance(curve.point(t), curve.point(unique.back())) > 2.0 * tolerance_) unique.push_back(t);
    const Vec3 a = curve.point(range.lo), b = curve.point(range.hi);
    if (unique.empty() || distance(curve.point(unique.front()), a) > 2.0 * tolerance_) unique.insert(unique.begin(), range.lo);
    if (distance(curve.point(unique.back()), b) > 2.0 * tolerance_ || unique.size() == 1) unique.push_back(range.hi);
    for (std::size_t i = 0; i + 1 < unique.size(); ++i) result.push_back({unique[i], unique[i + 1]});
    return result;
}

void BooleanBuilder::pairArcs(FaceId fa, FaceId fb) {
    const Surface &sa = *bodies_[0].face(fa).surface, &sb = *bodies_[1].face(fb).surface;
    if (isPlane(sa) && isPlane(sb)) {
        const Frame3 &pa = static_cast<const Plane &>(sa).frame(), &pb = static_cast<const Plane &>(sb).frame();
        if (norm(cross(pa.zDir(), pb.zDir())) <= 1e-12 && std::fabs(dot(pa.zDir(), pb.origin() - pa.origin())) <= tolerance_) {
            coplanarArcs(fa, fb);
            return;
        }
    }
    int k;
    if (isPlane(sa)) k = 0;
    else if (isPlane(sb)) k = 1;
    else {
        surfaceArcs(fa, fb);
        return;
    }
    const FaceId fp = k == 0 ? fa : fb, fq = k == 0 ? fb : fa;
    const Body &bp = bodies_[k], &bq = bodies_[1 - k];
    const Plane &plane = static_cast<const Plane &>(*bp.face(fp).surface);
    const PlaneSurfaceIntersection intersection =
        intersectPlaneSurface(plane, *bq.face(fq).surface, commonBounds(fa, fb), tolerance_);
    if (intersection.coincident) throw std::domain_error("booleanOperation: superficie non piana che contiene parte di un piano");
    rejectTangentLines(fa, fb, intersection.tangentCurves, intersection.tangentRanges);
    const Vec3 n = plane.frame().zDir();
    const double offset = dot(n, plane.frame().origin());

    for (std::size_t i = 0; i < intersection.curves.size(); ++i) {
        const CurvePtr<3> &curve = intersection.curves[i];
        const Interval &range = intersection.ranges[i];
        std::vector<double> parameters;
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
                    if (projection.distance <= 10.0 * tolerance_) parameters.push_back(projection.parameter);
                }
            }
        addArcs(curve, range, parameters, fa, fb);
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

// Contatto tangente dentro le due facce: non gestito (le facce si
// toccherebbero lungo una retta senza attraversarsi).
void BooleanBuilder::rejectTangentLines(FaceId fa, FaceId fb, const std::vector<CurvePtr<3>> &curves,
                                        const std::vector<Interval> &ranges) const {
    for (std::size_t i = 0; i < curves.size(); ++i)
        for (int j = 0; j <= 256; ++j)
            if (touchesBoth(fa, fb, curves[i]->point(ranges[i].lo + ranges[i].length() * j / 256.0)))
                throw std::domain_error("booleanOperation: contatto tangente tra facce non ancora gestito");
}

// Tratti della curva (divisa nei parametri dati) che stanno in entrambe le facce.
void BooleanBuilder::addArcs(const CurvePtr<3> &curve, const Interval &range, const std::vector<double> &parameters, FaceId fa,
                             FaceId fb, const IntersectionCurve *source) {
    bool added = false;
    for (const Interval &piece : splitRange(*curve, range, parameters)) {
        const Vec3 middle = curve->point(0.5 * (piece.lo + piece.hi));
        // Tratto degenere (una curva chiusa intera ha il punto medio lontano dagli estremi).
        if (distance(curve->point(piece.lo), curve->point(piece.hi)) <= tolerance_ && distance(middle, curve->point(piece.lo)) <= tolerance_)
            continue;
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
        arcs_.push_back(arc);
        added = true;
    }
    if (added && source && source->pcurves[0]) {
        PCurves entry;
        entry.surface[0] = bodies_[0].face(fa).surface.get();
        entry.surface[1] = bodies_[1].face(fb).surface.get();
        entry.pcurve[0] = source->pcurves[0];
        entry.pcurve[1] = source->pcurves[1];
        entry.deviation = source->deviation;
        pcurves_[curve.get()] = entry;
    }
}

// Due superfici non piane: curve d'intersezione tracciate (fk_marching) a
// partire dai punti in cui gli edge di una faccia attraversano la superficie
// dell'altra dentro di essa.
void BooleanBuilder::surfaceArcs(FaceId fa, FaceId fb) {
    std::vector<Vec3> crossings;
    auto collect = [&](int k, FaceId self, FaceId otherFace) {
        const Body &body = bodies_[k], &other = bodies_[1 - k];
        const Surface &otherSurface = *other.face(otherFace).surface;
        for (LoopId l : body.face(self).loops)
            for (FinId f : body.loopFins(l)) {
                const Edge &edge = body.edge(body.fin(f).edge);
                const CurveSurfaceIntersection hits = intersectCurveSurface(*edge.curve, edge.range, otherSurface, tolerance_);
                for (const Interval &piece : hits.coincident)
                    if (classifyPointOnFace(other, otherFace, edge.curve->point(0.5 * (piece.lo + piece.hi)), tolerance_) != PointLocation::Outside)
                        throw std::domain_error("booleanOperation: edge che giace su una superficie non piana dell'altro solido non ancora gestito");
                for (double t : hits.parameters) {
                    const Vec3 x = edge.curve->point(t);
                    if (classifyPointOnFace(other, otherFace, x, tolerance_) != PointLocation::Outside) crossings.push_back(x);
                }
            }
    };
    collect(0, fa, fb);
    collect(1, fb, fa);

    SurfaceIntersectionOptions options;
    options.tolerance = tolerance_;
    const SurfaceIntersection intersection =
        intersectSurfaces(*bodies_[0].face(fa).surface, *bodies_[1].face(fb).surface, commonBounds(fa, fb), crossings, options);
    if (intersection.coincident) throw std::domain_error("booleanOperation: superfici non piane coincidenti non ancora gestite");
    for (const Vec3 &x : intersection.tangentPoints)
        if (touchesBoth(fa, fb, x)) throw std::domain_error("booleanOperation: contatto tangente tra facce non ancora gestito");
    std::vector<CurvePtr<3>> tangentCurves;
    std::vector<Interval> tangentRanges;
    for (const IntersectionCurve &line : intersection.tangentCurves) {
        tangentCurves.push_back(line.curve);
        tangentRanges.push_back(line.range);
    }
    rejectTangentLines(fa, fb, tangentCurves, tangentRanges);

    for (const IntersectionCurve &curve : intersection.curves) {
        std::vector<double> parameters;
        for (const Vec3 &x : crossings) {
            const CurveProjection<3> projection = projectPoint(*curve.curve, x, curve.range);
            if (projection.distance <= 10.0 * tolerance_) parameters.push_back(projection.parameter);
        }
        addArcs(curve.curve, curve.range, parameters, fa, fb, &curve);
    }
}

// Facce complanari: il bordo di ciascuna, dove passa dentro l'altra, la taglia.
void BooleanBuilder::coplanarArcs(FaceId fa, FaceId fb) {
    for (int k = 0; k < 2; ++k) {
        const FaceId fx = k == 0 ? fa : fb, fy = k == 0 ? fb : fa;
        const Body &bx = bodies_[k], &by = bodies_[1 - k];
        const Plane &plane = static_cast<const Plane &>(*bx.face(fx).surface);
        for (LoopId l : by.face(fy).loops)
            for (FinId f : by.loopFins(l)) {
                const Edge &edge = by.edge(by.fin(f).edge);
                const CurvePtr<2> planar = exactPCurve(plane, edge.curve, edge.range, std::max(tolerance_, 1e-9 * scale_));
                if (!planar) throw std::logic_error("booleanOperation: edge fuori dal piano complanare");
                std::vector<double> parameters;
                for (LoopId lx : bx.face(fx).loops)
                    for (FinId fxFin : bx.loopFins(lx)) {
                        const Edge &other = bx.edge(bx.fin(fxFin).edge);
                        for (const CurveCurvePoint &p : intersectCurves(*planar, edge.range, *bx.fin(fxFin).pcurve, other.range, tolerance_).points)
                            parameters.push_back(p.s);
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
                    arcs_.push_back(arc);
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

void BooleanBuilder::finishCycle(const Surface &surface, bool sense, Cycle &cycle) const {
    constexpr int samples = 12;
    std::vector<Vec3> points;
    for (const Piece &piece : cycle.pieces)
        for (int j = 0; j < samples; ++j) points.push_back(piece.pointAt(double(j) / samples));
    points.push_back(points.front());
    const double period = surface.isUPeriodic() ? surface.uPeriod() : 0.0;
    std::vector<Vec2> &polygon = cycle.polygon;
    if (isPlane(surface)) {
        const Frame3 &frame = static_cast<const Plane &>(surface).frame();
        for (const Vec3 &p : points) {
            const Vec3 local = frame.toLocal(p);
            polygon.push_back(Vec2(local.x(), local.y()));
        }
    } else {
        const SurfaceProjection first = projectPoint(surface, points.front());
        Vec2 uv(first.u, first.v);
        for (const Vec3 &p : points) {
            if (!polygon.empty() && !invertPoint(surface, p, uv, 10.0 * tolerance_ + 1e-9 * scale_, scale_))
                throw std::domain_error("booleanOperation: bordo di una faccia fuori dalla superficie");
            polygon.push_back(uv);
        }
    }
    const double du = polygon.back()[0] - polygon.front()[0];
    polygon.pop_back();
    cycle.wrap = period > 0.0 ? int(std::lround(du / period)) : 0;
    double area = 0.0, vdu = 0.0;
    for (std::size_t i = 0; i < polygon.size(); ++i) {
        Vec2 a = polygon[i], b = polygon[(i + 1) % polygon.size()];
        if (i + 1 == polygon.size()) b[0] += cycle.wrap * period;  // chiusura di un ciclo avvolto
        area += cross(a, b);
        vdu += 0.5 * (a[1] + b[1]) * (b[0] - a[0]);
    }
    cycle.area = 0.5 * area * (sense ? 1.0 : -1.0);  // positiva per i contorni esterni
    cycle.meanV = cycle.wrap != 0 ? vdu / (cycle.wrap * period) : 0.0;
}

// Semiretta verticale nello spazio (u, v) contro i poligoni dei cicli: il
// primo attraversamento sopra (o sotto) dice da che parte sta il dominio.
bool verticalRayInside(const std::vector<const Cycle *> &cycles, const Vec2 &uv, double period, double sign) {
    double above = 1e300, below = -1e300;
    int aboveOrientation = 0, belowOrientation = 0;
    for (const Cycle *cycle : cycles) {
        const std::vector<Vec2> &polygon = cycle->polygon;
        for (std::size_t i = 0; i < polygon.size(); ++i) {
            Vec2 a = polygon[i], b = polygon[(i + 1) % polygon.size()];
            if (i + 1 == polygon.size()) b[0] += cycle->wrap * period;
            if (a[0] == b[0]) continue;
            const double lo = std::min(a[0], b[0]), hi = std::max(a[0], b[0]);
            int kLo = 0, kHi = 0;
            if (period > 0.0) {
                kLo = int(std::floor((lo - uv[0]) / period)) - 1;
                kHi = int(std::ceil((hi - uv[0]) / period)) + 1;
            }
            for (int k = kLo; k <= kHi; ++k) {
                const double u = uv[0] + k * period;
                if (!(u > lo && u <= hi)) continue;
                const double v = a[1] + (b[1] - a[1]) * (u - a[0]) / (b[0] - a[0]);
                const int orientation = (b[0] > a[0] ? 1 : -1) * int(sign);
                if (v > uv[1] && v < above) {
                    above = v;
                    aboveOrientation = orientation;
                } else if (v <= uv[1] && v > below) {
                    below = v;
                    belowOrientation = orientation;
                }
            }
        }
    }
    if (aboveOrientation != 0) return aboveOrientation < 0;
    if (belowOrientation != 0) return belowOrientation > 0;
    return false;
}

bool BooleanBuilder::insideSubFace(const SubFace &subFace, const Vec2 &uv) const {
    const Face &face = bodies_[subFace.body].face(subFace.face);
    std::vector<const Cycle *> cycles;
    for (const Cycle &cycle : subFace.cycles) cycles.push_back(&cycle);
    return verticalRayInside(cycles, uv, face.surface->isUPeriodic() ? face.surface->uPeriod() : 0.0, face.sense ? 1.0 : -1.0);
}

std::vector<SubFace> BooleanBuilder::buildSubFaces(int k, FaceId f, const std::vector<Piece> &cuts) const {
    const Body &body = bodies_[k];
    const Face &face = body.face(f);
    const Surface &surface = *face.surface;
    if (surface.isVPeriodic()) throw std::domain_error("booleanOperation: superfici periodiche in v non gestite");

    // Semilati: pezzi di bordo nel verso della fin, tagli nei due versi.
    std::vector<Piece> halfEdges;
    for (LoopId l : face.loops)
        for (FinId finId : body.loopFins(l)) {
            const Fin &fin = body.fin(finId);
            const Edge &edge = body.edge(fin.edge);
            for (const Interval &range : splitAtPoints(*edge.curve, edge.range)) {
                Piece piece;
                piece.curve = edge.curve;
                piece.range = range;
                piece.forward = fin.sense;
                piece.tolerance = edge.tolerance;
                halfEdges.push_back(piece);
            }
        }
    for (const Piece &cut : cuts) {
        halfEdges.push_back(cut);
        halfEdges.push_back(cut.reversed());
    }

    std::vector<Vec3> nodes;
    auto nodeOf = [&](const Vec3 &p) {
        for (std::size_t i = 0; i < nodes.size(); ++i)
            if (distance(nodes[i], p) <= tolerance_) return int(i);
        nodes.push_back(p);
        return int(nodes.size()) - 1;
    };
    const std::size_t count = halfEdges.size();
    std::vector<int> from(count), to(count);
    for (std::size_t h = 0; h < count; ++h) {
        from[h] = nodeOf(halfEdges[h].start());
        to[h] = nodeOf(halfEdges[h].end());
    }
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
        normals[v] = faceNormal(surface, face.sense, nodes[v]);
        if (outgoing[v].empty()) continue;
        references[v] = normalized(project(halfEdges[outgoing[v].front()].startTangent(), normals[v]));
        std::vector<double> angles;
        for (int h : outgoing[v]) angles.push_back(angleOf(int(v), halfEdges[h].startTangent()));
        for (std::size_t i = 0; i < angles.size(); ++i)
            for (std::size_t j = i + 1; j < angles.size(); ++j)
                if (std::fabs(angles[i] - angles[j]) < 1e-9) useChords[v] = true;
    }
    // Direzione di partenza: la tangente, oppure (tangenze) la corda verso un
    // punto vicino del tratto.
    auto startDirection = [&](int h) {
        const Piece &piece = halfEdges[h];
        if (!useChords[from[h]]) return piece.startTangent();
        return piece.pointAt(1e-3) - piece.start();
    };
    auto endDirection = [&](int h) {
        const Piece &piece = halfEdges[h];
        if (!useChords[to[h]]) return -piece.endTangent();
        return piece.pointAt(1.0 - 1e-3) - piece.end();
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
            if (rotation <= 1e-12) rotation = kTwoPi;  // tornare indietro: solo se non c'e' altro
            if (rotation < bestRotation) {
                bestRotation = rotation;
                best = o;
            }
        }
        return best;
    };

    std::vector<Cycle> cycles;
    std::vector<bool> used(count, false);
    for (std::size_t start = 0; start < count; ++start) {
        if (used[start]) continue;
        Cycle cycle;
        int h = int(start);
        for (std::size_t guard = 0;; ++guard) {
            if (guard > count || h < 0 || used[h]) {
                throw std::domain_error("booleanOperation: divisione di una faccia non riuscita");
            }
            used[h] = true;
            cycle.pieces.push_back(halfEdges[h]);
            h = next(h);
            if (h == int(start)) break;
        }
        finishCycle(surface, face.sense, cycle);
        cycles.push_back(std::move(cycle));
    }

    // Raggruppamento: contorni esterni con i loro fori; sulle superfici
    // periodiche anche fasce tra due cicli avvolti.
    const double period = surface.isUPeriodic() ? surface.uPeriod() : 0.0;
    const double sign = face.sense ? 1.0 : -1.0;
    std::vector<int> outers, holes, wrapping;
    for (std::size_t i = 0; i < cycles.size(); ++i) {
        if (cycles[i].wrap != 0) {
            if (std::abs(cycles[i].wrap) > 1) throw std::domain_error("booleanOperation: ciclo avvolto piu' volte");
            wrapping.push_back(int(i));
        } else {
            (cycles[i].area > 0.0 ? outers : holes).push_back(int(i));
        }
    }
    std::vector<SubFace> result;
    std::vector<std::vector<int>> members;
    for (int o : outers) members.push_back({o});
    // A pari quota (un taglio chiuso percorso nei due versi) prima il ciclo
    // che chiude la fascia inferiore (dominio sotto), poi quello che apre la successiva.
    std::sort(wrapping.begin(), wrapping.end(), [&](int a, int b) {
        const double gap = cycles[a].meanV - cycles[b].meanV;
        if (std::fabs(gap) > 1e-9 * (1.0 + std::fabs(cycles[a].meanV))) return gap < 0.0;
        return int(sign) * cycles[a].wrap < int(sign) * cycles[b].wrap;
    });
    if (wrapping.size() % 2) throw std::domain_error("booleanOperation: cicli avvolti spaiati");
    const std::size_t bandStart = members.size();
    for (std::size_t i = 0; i < wrapping.size(); i += 2) {
        // Dominio sopra il ciclo inferiore (avanza in u) e sotto quello superiore.
        const int lower = wrapping[i], upper = wrapping[i + 1];
        if (int(sign) * cycles[lower].wrap != 1 || int(sign) * cycles[upper].wrap != -1)
            throw std::domain_error("booleanOperation: fascia non chiusa su una superficie periodica");
        members.push_back({lower, upper});
    }
    for (int h : holes) {
        // Punto appena dentro il dominio del foro (a sinistra del ciclo
        // nell'orientamento della faccia): un punto sul foro stesso starebbe
        // anche sul bordo di un eventuale disco con lo stesso contorno.
        const std::vector<Vec2> &polygon = cycles[h].polygon;
        std::size_t longest = 0;
        for (std::size_t i = 0; i < polygon.size(); ++i)
            if (distance(polygon[i], polygon[(i + 1) % polygon.size()]) >
                distance(polygon[longest], polygon[(longest + 1) % polygon.size()]))
                longest = i;
        const Vec2 a = polygon[longest], b = polygon[(longest + 1) % polygon.size()];
        const Vec2 probe = 0.5 * (a + b) + (1e-3 * sign) * perpendicular(b - a);
        int best = -1;
        double bestArea = 1e300;
        for (std::size_t m = 0; m < members.size(); ++m) {
            std::vector<const Cycle *> boundary;
            for (int c : members[m]) boundary.push_back(&cycles[c]);
            // Il punto del foro deve stare nella regione delimitata dal solo contorno.
            if (!verticalRayInside(boundary, probe, period, sign)) continue;
            const double size = m < bandStart ? cycles[members[m][0]].area : 1e299;
            if (size < bestArea) {
                bestArea = size;
                best = int(m);
            }
        }
        if (best < 0) throw std::domain_error("booleanOperation: foro fuori da ogni contorno");
        members[best].push_back(h);
    }
    for (const std::vector<int> &group : members) {
        SubFace subFace;
        subFace.body = k;
        subFace.face = f;
        for (int c : group) subFace.cycles.push_back(cycles[c]);
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

    // Facce complanari dell'altro body: prima di tutto si guarda se il pezzo
    // ci sta sopra (le regole locali darebbero dentro/fuori a caso).
    std::vector<FaceId> coplanar;
    Vec3 planeNormal;
    if (isPlane(surface)) {
        const Frame3 &frame = static_cast<const Plane &>(surface).frame();
        planeNormal = face.sense ? frame.zDir() : -frame.zDir();
        for (FaceId g : otherBody.faces()) {
            const Surface &gs = *otherBody.face(g).surface;
            if (!isPlane(gs)) continue;
            const Frame3 &gf = static_cast<const Plane &>(gs).frame();
            if (norm(cross(gf.zDir(), frame.zDir())) <= 1e-9 && std::fabs(dot(gf.zDir(), frame.origin() - gf.origin())) <= tolerance_)
                coplanar.push_back(g);
        }
    }

    // Punto interno vicino al tratto piu' lungo, lontano dai bordi delle facce complanari.
    const Piece *longest = nullptr;
    double longestLength = -1.0;
    for (const Cycle &cycle : subFace.cycles)
        for (const Piece &piece : cycle.pieces) {
            const double length = distance(piece.start(), piece.middle()) + distance(piece.middle(), piece.end());
            if (length > longestLength) {
                longestLength = length;
                longest = &piece;
            }
        }
    Vec3 x;
    const Vec3 w = inward(*longest, x);
    auto interiorPoints = [&](const std::function<bool(const Vec3 &)> &accept) {
        for (double epsilon = std::min(0.05 * longestLength, 1e-2 * scale_); epsilon > 2.0 * tolerance_; epsilon *= 0.25) {
            const SurfaceProjection projection = projectPoint(surface, x + epsilon * w);
            Vec2 uv(projection.u, projection.v);
            if (isPlane(surface)) {
                const Vec3 local = static_cast<const Plane &>(surface).frame().toLocal(projection.point);
                uv = Vec2(local.x(), local.y());
            }
            if (insideSubFace(subFace, uv) && accept(projection.point)) return true;
        }
        return false;
    };

    if (!coplanar.empty()) {
        Location on = Location::Out;
        bool found = false;
        const bool ok = interiorPoints([&](const Vec3 &z) {
            for (FaceId g : coplanar) {
                const PointLocation location = classifyPointOnFace(otherBody, g, z, tolerance_);
                if (location == PointLocation::Boundary) return false;
                if (location == PointLocation::Inside) {
                    const Frame3 &gf = static_cast<const Plane &>(*otherBody.face(g).surface).frame();
                    const Vec3 gn = otherBody.face(g).sense ? gf.zDir() : -gf.zDir();
                    on = dot(gn, planeNormal) > 0.0 ? Location::OnSame : Location::OnOpposite;
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
            if (!piece.cut || piece.coplanar) continue;
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

Body BooleanBuilder::run() {
    for (FaceId fa : bodies_[0].faces())
        for (FaceId fb : bodies_[1].faces())
            if (boxes_[0].at(fa.index).overlaps(boxes_[1].at(fb.index))) pairArcs(fa, fb);

    for (const Arc &arc : arcs_) {
        vertexPoints_.push_back(arc.curve->point(arc.range.lo));
        vertexPoints_.push_back(arc.curve->point(arc.range.hi));
    }

    // Tagli di ogni faccia, divisi nei punti dei vertici e senza doppioni.
    std::map<int, std::vector<Piece>> cuts[2];
    for (const Arc &arc : arcs_)
        for (int k = 0; k < 2; ++k) {
            if (!arc.cut[k]) continue;
            for (const Interval &range : splitAtPoints(*arc.curve, arc.range)) {
                Piece piece;
                piece.curve = arc.curve;
                piece.range = range;
                piece.cut = true;
                piece.partner = arc.face[1 - k];
                piece.coplanar = arc.coplanar;
                std::vector<Piece> &list = cuts[k][arc.face[k].index];
                bool duplicate = false;
                for (Piece &existing : list) {
                    const bool sameEnds = (distance(existing.start(), piece.start()) <= tolerance_ && distance(existing.end(), piece.end()) <= tolerance_)
                        || (distance(existing.start(), piece.end()) <= tolerance_ && distance(existing.end(), piece.start()) <= tolerance_);
                    if (sameEnds && distance(existing.middle(), piece.middle()) <= 10.0 * tolerance_) {
                        duplicate = true;
                        if (existing.coplanar && !piece.coplanar) existing = piece;  // meglio un taglio trasversale
                        break;
                    }
                }
                if (!duplicate) list.push_back(piece);
            }
        }

    std::vector<std::pair<SubFace, bool>> kept;  // pezzo e "da girare"
    for (int k = 0; k < 2; ++k) {
        const SolidClassifier other(bodies_[1 - k], tolerance_);
        for (FaceId f : bodies_[k].faces()) {
            const auto found = cuts[k].find(f.index);
            const std::vector<Piece> none;
            for (SubFace &subFace : buildSubFaces(k, f, found != cuts[k].end() ? found->second : none)) {
                const Location location = classify(subFace, other);
                if (keep(k, location)) kept.emplace_back(std::move(subFace), operation_ == BooleanOperation::Subtract && k == 1);
            }
        }
    }
    if (kept.empty()) return Body();

    std::vector<Vec3> vertices;
    auto vertexOf = [&](const Vec3 &p) {
        for (std::size_t i = 0; i < vertices.size(); ++i)
            if (distance(vertices[i], p) <= tolerance_) return int(i);
        vertices.push_back(p);
        return int(vertices.size()) - 1;
    };
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
            std::vector<Body::BuildFin> loop;
            for (const Piece &piece : pieces) {
                const int vs = vertexOf(piece.start()), ve = vertexOf(piece.end());
                const Vec3 middle = piece.middle();
                int index = -1;
                for (std::size_t e = 0; e < edges.size(); ++e) {
                    const bool sameEnds = (edges[e].start == vs && edges[e].end == ve) || (edges[e].start == ve && edges[e].end == vs);
                    if (sameEnds && distance(edgeMiddles[e], middle) <= 10.0 * tolerance_) {
                        index = int(e);
                        break;
                    }
                }
                if (index < 0) {
                    Body::BuildEdge edge;
                    edge.curve = piece.curve;
                    edge.range = piece.range;
                    edge.start = vertexOf(piece.curve->point(piece.range.lo));
                    edge.end = vertexOf(piece.curve->point(piece.range.hi));
                    edge.tolerance = piece.tolerance;
                    edges.push_back(edge);
                    edgeMiddles.push_back(middle);
                    index = int(edges.size()) - 1;
                }
                // Verso della fin: tangente di percorrenza contro quella dell'edge nel punto medio.
                const Body::BuildEdge &edge = edges[index];
                const Vec3 edgeTangent = edge.curve->derivative(0.5 * (edge.range.lo + edge.range.hi));
                const CurveProjection<3> onPiece = projectPoint(*piece.curve, edgeMiddles[index], piece.range);
                loop.push_back({index, dot(piece.tangentAt(onPiece.parameter), edgeTangent) > 0.0});
            }
            built.loops.push_back(std::move(loop));
        }
        faces.push_back(std::move(built));
    }
    Body result;
    try {
        result = Body::build(vertices, edges, faces);
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
    computePCurves(result);
    const std::vector<CheckIssue> issues = checkBody(result);
    if (!issues.empty())
        throw std::domain_error("booleanOperation: risultato non valido (" + describe(issues.front().code) + ": " + issues.front().message + ")");
    return result;
}

}

Body booleanOperation(const Body &a, const Body &b, BooleanOperation operation, const BooleanOptions &options) {
    return BooleanBuilder(a, b, operation, options.tolerance).run();
}

}
