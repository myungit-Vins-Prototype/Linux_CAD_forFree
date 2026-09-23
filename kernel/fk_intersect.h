#ifndef FORGECAD_FK_INTERSECT_H
#define FORGECAD_FK_INTERSECT_H

#include <vector>

#include "fk_surface.h"

// Intersezioni esatte (o raffinate con Newton fino all'arrotondamento) tra
// curve, piani e superfici: la base delle booleane (fk_boolean.h).
//
// Casi gestiti in questa versione:
//  - curva con piano (2D: con retta), per rette, coniche, B-spline e NURBS,
//    curve limitate e trasformate: tutte le radici (Bernstein);
//  - curva con curva nel piano: con una retta o una conica in forma chiusa /
//    per sostituzione nell'equazione implicita della conica (Bernstein),
//    B-spline con B-spline per suddivisione delle pezze di Bezier + Newton;
//  - piano con piano, cilindro, sfera, superficie estrusa (qualsiasi curva
//    base): curve esatte (rette, cerchi, ellissi, NURBS);
//  - retta con piano, cilindro, sfera, superficie estrusa (per il test
//    punto-in-solido).
// Le intersezioni tra due superfici non piane richiedono il "marching" e
// non ci sono ancora: chi le chiede riceve std::domain_error.
namespace ForgeCad::Kernel {

struct Box {
    Vec3 lo{1e300, 1e300, 1e300}, hi{-1e300, -1e300, -1e300};
    void add(const Vec3 &p);
    void add(const Box &other);
    Box padded(double margin) const;
    bool isEmpty() const { return lo.x() > hi.x(); }
    bool overlaps(const Box &other) const;
    double diagonal() const { return isEmpty() ? 0.0 : distance(lo, hi); }
};

// Box che contiene il tratto di curva (dai poli per le B-spline, esatto per
// rette e coniche).
Box curveBox(const Curve<3> &curve, const Interval &range);

// Parametri t in range con normal . C(t) = offset (N = 2: C sulla retta; N = 3:
// C sul piano). I tratti in cui la curva giace sul piano (entro tolerance)
// vanno in `coincident` e non producono radici isolate.
template <int N>
struct PlaneRoots {
    std::vector<double> parameters;
    std::vector<Interval> coincident;
};
template <int N>
PlaneRoots<N> planeRoots(const Curve<N> &curve, const Interval &range, const Vec<N> &normal, double offset,
                         double tolerance);

struct CurveCurvePoint {
    double s = 0.0, t = 0.0;  // parametri sulla prima e sulla seconda curva
    Vec2 point;
};
struct CurveCurveIntersection {
    std::vector<CurveCurvePoint> points;  // anche gli estremi dei tratti sovrapposti
    bool overlap = false;                 // le curve hanno un tratto in comune
};
// Intersezione di due curve del piano sui due tratti.
CurveCurveIntersection intersectCurves(const Curve<2> &a, const Interval &aRange, const Curve<2> &b, const Interval &bRange,
                                       double tolerance);

// Curve d'intersezione tra un piano e una superficie. Le rette sono limitate
// alla parte dentro `bounds`.
struct PlaneSurfaceIntersection {
    std::vector<CurvePtr<3>> curves;
    std::vector<Interval> ranges;
    bool coincident = false;  // la superficie contiene una parte del piano (o e' il piano)
    bool tangent = false;     // contatto tangente
    // Rette di tangenza (limitate a `bounds`), separate dalle curve di taglio.
    std::vector<CurvePtr<3>> tangentCurves;
    std::vector<Interval> tangentRanges;
};
PlaneSurfaceIntersection intersectPlaneSurface(const Plane &plane, const Surface &surface, const Box &bounds,
                                               double tolerance);

// Parametri t delle intersezioni della retta origin + t direction (unitaria)
// con la superficie (intera). `grazing`: la retta e' tangente o giace sulla
// superficie, e il conteggio delle intersezioni non e' affidabile.
std::vector<double> intersectLineSurface(const Vec3 &origin, const Vec3 &direction, const Surface &surface, bool &grazing,
                                         double tolerance);

}

#endif
