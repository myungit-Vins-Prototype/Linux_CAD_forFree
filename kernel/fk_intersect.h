#ifndef FORGECAD_FK_INTERSECT_H
#define FORGECAD_FK_INTERSECT_H

#include <functional>
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
//  - curva con piano, cilindro, superficie estrusa (riduzione esatta a
//    un'intersezione curva-curva nel piano normale alla direzione).
// Le intersezioni tra due superfici non piane sono in fk_marching.h.
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
// Intersezione di due curve del piano sui due tratti. Punti coincidenti
// entro tolerance compaiono una volta sola; con distinctParameters restano
// distinti se hanno parametri diversi sulla prima curva (curve che passano
// due volte per lo stesso punto).
CurveCurveIntersection intersectCurves(const Curve<2> &a, const Interval &aRange, const Curve<2> &b, const Interval &bRange,
                                       double tolerance, bool distinctParameters = false);

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

// Tratto della retta origin + t direction dentro il box (falso se non c'e').
bool clipLineToBox(const Vec3 &origin, const Vec3 &direction, const Box &box, Interval &range);

// Cilindro generalizzato S(u, v) = C(u) + v D (D unitaria): il cilindro
// (C = cerchio di base, u = angolo) e la superficie estrusa. Falso per le
// altre superfici.
struct GeneralizedCylinder {
    CurvePtr<3> profile;
    Interval domain;  // dominio di u
    Vec3 direction;
    bool periodic = false;
};
bool generalizedCylinder(const Surface &surface, GeneralizedCylinder &out);

// Sistema con asse z = direction (unitaria) e origine data.
Frame3 normalFrame(const Vec3 &direction, const Vec3 &origin);

// Immagine di una curva nella proiezione ortogonale sul piano xy di `frame`
// (le coordinate 2D sono quelle locali x, y). Esatta: rette e coniche in un
// piano parallelo restano del loro tipo con lo stesso parametro, le altre
// coniche passano per la NURBS esatta, le B-spline conservano il parametro.
struct PlanarImage {
    CurvePtr<2> curve;  // nullptr: la curva si riduce a `point` (retta parallela a z)
    Interval range;
    Vec2 point;
    std::function<double(double)> toCurve;  // parametro dell'immagine -> parametro della curva
};
PlanarImage planarImage(const Curve<3> &curve, const Interval &range, const Frame3 &frame);

// Parametri della curva (nel tratto) in cui incontra la superficie: piano,
// cilindro o superficie estrusa (std::domain_error per le altre). I tratti
// che giacciono sulla superficie vanno in `coincident`.
struct CurveSurfaceIntersection {
    std::vector<double> parameters;
    std::vector<Interval> coincident;
};
CurveSurfaceIntersection intersectCurveSurface(const Curve<3> &curve, const Interval &range, const Surface &surface,
                                               double tolerance);

}

#endif
