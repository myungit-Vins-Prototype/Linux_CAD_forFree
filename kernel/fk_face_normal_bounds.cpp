#include "fk_face_normal_bounds.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

#include "fk_bspline_surface.h"
#include "fk_nurbs.h"
#include "fk_precision.h"

namespace ForgeCad::Kernel {
namespace {

// Intervalli arrotondati verso l'esterno: servono soltanto a certificare
// il verso dei contorni, senza stimare l'area con campioni o quadrature.
struct Bound {
    double lo, hi;
    explicit Bound(double value = 0.0) : lo(value), hi(value) {}
    Bound(double low, double high) : lo(low), hi(high) {}
    bool finite() const { return std::isfinite(lo) && std::isfinite(hi) && lo <= hi; }
};
double down(double x) { return std::nextafter(x, -std::numeric_limits<double>::infinity()); }
double up(double x) { return std::nextafter(x, std::numeric_limits<double>::infinity()); }
Bound operator+(Bound a, Bound b) { return {down(a.lo+b.lo),up(a.hi+b.hi)}; }
Bound operator-(Bound a, Bound b) { return {down(a.lo-b.hi),up(a.hi-b.lo)}; }
Bound operator-(Bound a) { return {-a.hi,-a.lo}; }
Bound operator*(Bound a, Bound b) {
    const double p[] = {a.lo*b.lo,a.lo*b.hi,a.hi*b.lo,a.hi*b.hi};
    return {down(*std::min_element(p,p+4)),up(*std::max_element(p,p+4))};
}
Bound dividePositive(Bound a, Bound b) {
    if (!(b.lo > 0.0) || !b.finite()) return {-INFINITY,INFINITY};
    return a * Bound(down(1.0/b.hi),up(1.0/b.lo));
}
struct PointBound { Bound x,y; };
PointBound position(const Vec3 &p, const Vec2 &origin) {
    return {Bound(p.x())-Bound(origin.x()),Bound(p.y())-Bound(origin.y())};
}
PointBound operator-(PointBound a, PointBound b) { return {a.x-b.x,a.y-b.y}; }
PointBound operator*(PointBound a, Bound b) { return {a.x*b,a.y*b}; }
Bound crossBound(PointBound a, PointBound b) { return a.x*b.y-a.y*b.x; }
Bound binomial(int n, int k) {
    Bound value(1.0);
    k = std::min(k,n-k);
    for (int i=1;i<=k;++i) value = dividePositive(value*Bound(double(n-k+i)),Bound(double(i)));
    return value;
}
bool constantWeights(const BSplineCurve<3> &spline) {
    const double weight = spline.weight(0);
    for (int i=0;i<spline.poleCount();++i)
        if (!std::isfinite(spline.weight(i)) || spline.weight(i) <= 0.0 || spline.weight(i) != weight) return false;
    return true;
}
bool polynomialCurve(const Curve<3> &curve) {
    if (curve.type() == CurveType::Trimmed) return polynomialCurve(*static_cast<const TrimmedCurve<3> &>(curve).basis());
    if (curve.type() == CurveType::Transformed) return polynomialCurve(*static_cast<const TransformedCurve &>(curve).basis());
    if (curve.type() == CurveType::Line) return true;
    return curve.type() == CurveType::BSpline && constantWeights(static_cast<const BSplineCurve<3> &>(curve));
}

// Integrale orientato x dy - y dx della pezza, sul parametro normalizzato.
// Per i polinomi e' il prodotto esatto di basi Bernstein, racchiuso in
// intervalli di arrotondamento. Per le SP-curve razionali positive si usa un
// bound conservativo di cross(A,A')/W^2, con A = W*(P-origin).
Bound areaBound(const BSplineCurve<3> &piece, const Vec2 &origin) {
    const int n = piece.degree();
    if (n < 1 || piece.poleCount() != n+1) return {-INFINITY,INFINITY};
    std::vector<PointBound> poles;
    for (const auto &p : piece.poles()) poles.push_back(position(p,origin));
    if (constantWeights(piece)) {
        Bound integral;
        for (int i=0;i<=n;++i) for (int j=0;j<n;++j) {
            const Bound coefficient = dividePositive(binomial(n,i)*binomial(n-1,j),
                                                       Bound(double(2*n))*binomial(2*n-1,i+j));
            integral = integral + coefficient*crossBound(poles[i],(poles[j+1]-poles[j])*Bound(double(n)));
        }
        return integral;
    }
    double minWeight = INFINITY, maxWeight = 0.0;
    for (int i=0;i<=n;++i) {
        const double w = piece.weight(i);
        if (!(w > 0.0) || !std::isfinite(w)) return {-INFINITY,INFINITY};
        minWeight = std::min(minWeight,w); maxWeight = std::max(maxWeight,w);
        poles[i] = poles[i]*Bound(w);
    }
    Bound numerator(INFINITY,-INFINITY);
    for (int i=0;i<=n;++i) for (int j=0;j<n;++j) {
        const auto term = crossBound(poles[i],(poles[j+1]-poles[j])*Bound(double(n)));
        numerator.lo = std::min(numerator.lo,term.lo); numerator.hi = std::max(numerator.hi,term.hi);
    }
    return dividePositive(numerator,Bound(minWeight,maxWeight)*Bound(minWeight,maxWeight));
}

// Sollevare una SP-curve nel piano z=0 permette di riusare il ritaglio
// omogeneo esatto del kernel. Il dominio di ogni wrapper va verificato:
// rationalBezierPieces non deve estrapolare o troncare un range non noto.
CurvePtr<3> liftedPCurve(const Curve<2> &curve, const Interval &range) {
    if (!range.isFinite() || !(range.hi > range.lo)) return {};
    if (curve.type() == CurveType::Trimmed) {
        const auto &trim = static_cast<const TrimmedCurve<2> &>(curve);
        if (range.lo < trim.domain().lo || range.hi > trim.domain().hi) return {};
        return liftedPCurve(*trim.basis(),range);
    }
    if (curve.type() == CurveType::Line) {
        const Vec2 a = curve.point(range.lo), b = curve.point(range.hi);
        if (!isFinite(a) || !isFinite(b)) return {};
        return std::make_shared<BSplineCurve<3>>(1,std::vector<double>{range.lo,range.lo,range.hi,range.hi},
            std::vector<Vec3>{{a.x(),a.y(),0.0},{b.x(),b.y(),0.0}});
    }
    if (curve.type() == CurveType::BSpline) {
        const auto &s = static_cast<const BSplineCurve<2> &>(curve);
        if (range.lo < s.domain().lo || range.hi > s.domain().hi) return {};
        std::vector<Vec3> poles;
        for (const auto &p : s.poles()) poles.push_back({p.x(),p.y(),0.0});
        return std::make_shared<BSplineCurve<3>>(s.degree(),s.knots(),std::move(poles),s.weights());
    }
    if (curve.type() == CurveType::Circle) {
        const auto &c = static_cast<const Circle<2> &>(curve);
        return std::make_shared<Circle<3>>(Vec3(c.center().x(),c.center().y(),0.0),
            Vec3(c.xAxis().x(),c.xAxis().y(),0.0),Vec3(c.yAxis().x(),c.yAxis().y(),0.0),c.radius());
    }
    if (curve.type() == CurveType::Ellipse) {
        const auto &c = static_cast<const Ellipse<2> &>(curve);
        return std::make_shared<Ellipse<3>>(Vec3(c.center().x(),c.center().y(),0.0),
            Vec3(c.xAxis().x(),c.xAxis().y(),0.0),Vec3(c.yAxis().x(),c.yAxis().y(),0.0),c.xRadius(),c.yRadius());
    }
    return {};
}

using Window = std::pair<Interval,Interval>;
struct BoundaryPiece { BSplineCurve<3> curve; bool forward; };
Window parameterWindow(const Body &body, FaceId id) {
    const Face &face = body.face(id);
    const Surface &surface = *face.surface;
    const Window full{surface.uDomain(),surface.vDomain()};
    // Avvolgimenti, seam e poli richiedono una prova topologica sul
    // rivestimento periodico; in loro presenza non restringere il supporto.
    if (surface.isUPeriodic() || surface.isVPeriodic() || face.loops.empty()) return full;
    Window bounds{{INFINITY,-INFINITY},{INFINITY,-INFINITY}};
    std::vector<std::vector<BoundaryPiece>> loops;
    for (LoopId loop : face.loops) {
        if (body.loop(loop).isolatedVertex.valid()) return full;
        loops.emplace_back();
        for (FinId fi : body.loopFins(loop)) {
            cancellationPoint();
            const Fin &fin = body.fin(fi);
            if (!fin.pcurve) return full;
            const Interval range = body.edge(fin.edge).range;
            const auto curve = liftedPCurve(*fin.pcurve,range);
            if (!curve) return full;
            auto pieces = rationalBezierPieces(*curve,range);
            if (pieces.empty()) return full;
            if (!fin.sense) std::reverse(pieces.begin(),pieces.end());
            for (auto &piece : pieces) {
                for (int i=0;i<piece.poleCount();++i) {
                    const auto &p = piece.poles()[i];
                    if (!isFinite(p) || !(piece.weight(i) > 0.0) || !std::isfinite(piece.weight(i))) return full;
                    bounds.first.lo = std::min(bounds.first.lo,p.x()); bounds.first.hi = std::max(bounds.first.hi,p.x());
                    bounds.second.lo = std::min(bounds.second.lo,p.y()); bounds.second.hi = std::max(bounds.second.hi,p.y());
                }
                loops.back().push_back({std::move(piece),fin.sense});
            }
        }
        if (loops.back().empty()) return full;
    }
    if (!bounds.first.isFinite() || !bounds.second.isFinite()
        || !(bounds.first.hi > bounds.first.lo) || !(bounds.second.hi > bounds.second.lo)) return full;
    if (bounds.first.lo < full.first.lo || bounds.first.hi > full.first.hi
        || bounds.second.lo < full.second.lo || bounds.second.hi > full.second.hi) return full;
    // Un contorno di soli fori descrive il complemento, non il rettangolo
    // del bordo. L'area orientata totale deve provare un dominio limitato.
    // Gli eventuali raccordi UV fra estremi di SP-curve sono dentro lo
    // stesso hull: chiudono soltanto gli arrotondamenti degli estremi.
    const Vec2 origin{bounds.first.lo+0.5*bounds.first.length(),bounds.second.lo+0.5*bounds.second.length()};
    Bound area;
    for (const auto &loop : loops) {
        Vec3 first,previous; bool started = false;
        for (const auto &piece : loop) {
            const auto &p = piece.curve.poles();
            const Vec3 start = piece.forward ? p.front() : p.back(), end = piece.forward ? p.back() : p.front();
            if (started) area = area + crossBound(position(previous,origin),position(start,origin));
            else { first = start; started = true; }
            const auto integral = areaBound(piece.curve,origin);
            area = area + (piece.forward ? integral : -integral);
            previous = end;
        }
        area = area + crossBound(position(previous,origin),position(first,origin));
    }
    if (!face.sense) area = -area;
    if (!area.finite() || !(area.lo > 0.0)) return full;
    return bounds;
}

bool polynomialProjection(const BSplineSurface &surface, const Vec3 &direction, const Window &window) {
    if (!window.first.isFinite() || !window.second.isFinite()
        || !(window.first.hi > window.first.lo) || !(window.second.hi > window.second.lo)) return false;
    const double weight = surface.weight(0,0);
    for (int i=0;i<surface.uPoleCount();++i) for (int j=0;j<surface.vPoleCount();++j)
        if (!std::isfinite(surface.weight(i,j)) || surface.weight(i,j) <= 0.0 || surface.weight(i,j) != weight) return false;
    const auto patches = surface.cachedBezierPatches();
    bool checked = false;
    for (const auto &patch : *patches) {
        // Contatto sul bordo incluso: non saltare pezze con intersezione
        // UV non vuota, ne' ritagliare la faccia in base a punti campionati.
        if (patch.uDomain().hi < window.first.lo || patch.uDomain().lo > window.first.hi
            || patch.vDomain().hi < window.second.lo || patch.vDomain().lo > window.second.hi) continue;
        cancellationPoint();
        checked = true;
        if (!(patch.uDomain().length() > 0.0) || !(patch.vDomain().length() > 0.0)) return false;
        std::vector<Vec3> du,dv;
        for (int i=0;i<patch.uPoleCount()-1;++i) for (int j=0;j<patch.vPoleCount();++j)
            du.push_back((patch.pole(i+1,j)-patch.pole(i,j))*(double(patch.uDegree())/patch.uDomain().length()));
        for (int i=0;i<patch.uPoleCount();++i) for (int j=0;j<patch.vPoleCount()-1;++j)
            dv.push_back((patch.pole(i,j+1)-patch.pole(i,j))*(double(patch.vDegree())/patch.vDomain().length()));
        if (du.empty() || dv.empty()) return false;
        for (const auto &u : du) {
            cancellationPoint();
            for (const auto &v : dv) {
                const double projection = dot(cross(u,v),direction), scale = norm(u)*norm(v);
                if (!std::isfinite(projection) || !std::isfinite(scale) || projection < -kAngularResolution*scale) return false;
            }
        }
    }
    return checked;
}

} // namespace

bool faceHasNonnegativeNormalProjection(const Body &body, FaceId id, const Vec3 &direction) {
    cancellationPoint();
    if (!body.contains(id) || !isFinite(direction) || !(norm(direction) > 0.0) || !std::isfinite(norm(direction))) return false;
    const Face &face = body.face(id);
    if (!face.surface) return false;
    const Vec3 outward = (face.sense ? 1.0 : -1.0)*normalized(direction);
    try {
        if (face.surface->type() == SurfaceType::BSpline) {
            const auto &spline = static_cast<const BSplineSurface &>(*face.surface);
            return polynomialProjection(spline,outward,parameterWindow(body,id));
        }
        if (face.surface->type() == SurfaceType::Extrusion) {
            const auto &extrusion = static_cast<const ExtrusionSurface &>(*face.surface);
            // Ogni normale dell'estrusione e' ortogonale alla generatrice,
            // anche per basi razionali/ellittiche, dominio V infinito o
            // SP-curve mancanti: questa identita' non usa i poli della base.
            if (norm(cross(extrusion.direction(),outward)) <= kAngularResolution) return true;
            if (!polynomialCurve(*extrusion.curve())) return false;
            const auto window = parameterWindow(body,id);
            if (!window.first.isFinite() || !window.second.isFinite()) return false;
            return polynomialProjection(toBSplineSurface(extrusion,window.first,window.second),outward,window);
        }
    } catch (const std::exception &) {
        return false;
    }
    return false;
}

}
