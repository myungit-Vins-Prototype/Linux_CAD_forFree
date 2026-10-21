#ifndef FORGECAD_FK_STORED_SURFACE_H
#define FORGECAD_FK_STORED_SURFACE_H
#include "fk_bspline.h"
#include "fk_bspline_surface.h"
#include "fk_sheet.h"
#include "fk_surface_algo.h"
// Confronto conservativo degli appoggi conservati dopo serializzazione.
// Usato da genealogia e anteprima; non cambia i criteri delle booleane.
namespace ForgeCad::Kernel {
// Identita' parametrica dei meridiani: non basta confrontare i puntatori
// dopo la rilettura delle cache. Conserviamo anche i trim e i parametri.
inline bool sameStoredCurve(const Curve<3> &a, const Curve<3> &b) {
    if (&a == &b) return true;
    if (a.type() != b.type()) return false;
    const auto sameVector = [](const Vec3 &x, const Vec3 &y) { return distance(x, y) == 0.0; };
    switch (a.type()) {
    case CurveType::Trimmed: {
        const auto &x = static_cast<const TrimmedCurve<3> &>(a), &y = static_cast<const TrimmedCurve<3> &>(b);
        return x.domain().lo == y.domain().lo && x.domain().hi == y.domain().hi
            && sameStoredCurve(*x.basis(), *y.basis());
    }
    case CurveType::Line: {
        const auto &x = static_cast<const Line<3> &>(a), &y = static_cast<const Line<3> &>(b);
        return sameVector(x.origin(), y.origin()) && sameVector(x.direction(), y.direction());
    }
    case CurveType::Circle: {
        const auto &x = static_cast<const Circle<3> &>(a), &y = static_cast<const Circle<3> &>(b);
        return x.radius() == y.radius() && sameVector(x.center(), y.center())
            && sameVector(x.xAxis(), y.xAxis()) && sameVector(x.yAxis(), y.yAxis());
    }
    case CurveType::Ellipse: {
        const auto &x = static_cast<const Ellipse<3> &>(a), &y = static_cast<const Ellipse<3> &>(b);
        return x.xRadius() == y.xRadius() && x.yRadius() == y.yRadius() && sameVector(x.center(), y.center())
            && sameVector(x.xAxis(), y.xAxis()) && sameVector(x.yAxis(), y.yAxis());
    }
    case CurveType::BSpline: {
        const auto &x = static_cast<const BSplineCurve<3> &>(a), &y = static_cast<const BSplineCurve<3> &>(b);
        if (x.degree() != y.degree() || x.knots() != y.knots() || x.poleCount() != y.poleCount()) return false;
        for (int i = 0; i < x.poleCount(); ++i)
            if (x.weight(i) != y.weight(i) || !sameVector(x.poles()[i], y.poles()[i])) return false;
        return true;
    }
    default: return false;
    }
}

inline bool sameStoredSpline(const BSplineSurface &a, const BSplineSurface &b) {
    if (a.uDegree() != b.uDegree() || a.vDegree() != b.vDegree()
        || a.uPoleCount() != b.uPoleCount() || a.vPoleCount() != b.vPoleCount()
        || a.uKnots() != b.uKnots() || a.vKnots() != b.vKnots()) return false;
    for (int i = 0; i < a.uPoleCount(); ++i)
        for (int j = 0; j < a.vPoleCount(); ++j) {
            if (a.weight(i, j) != b.weight(i, j)) return false;
            for (int axis = 0; axis < 3; ++axis)
                if (a.pole(i, j)[axis] != b.pole(i, j)[axis]) return false;
        }
    return true;
}

// La mitra puo' prolungare il polinomio terminale di una superficie di
// appoggio senza cambiarla nel dominio precedente. Riconosciamo soltanto
// la stessa estensione algebrica del kernel, confrontando nodi/poli/pesi;
// niente campioni geometrici che possano nascondere superfici nuove.
inline bool sameStoredSurface(const Surface &a, const Surface &b, double tolerance = 1e-6) {
    if (sameSurface(a, b, tolerance)) return true;
    if (a.type() != b.type()) return false;
    if (a.type() == SurfaceType::Revolution) {
        const auto &x = static_cast<const RevolutionSurface &>(a), &y = static_cast<const RevolutionSurface &>(b);
        return distance(x.axisPoint(), y.axisPoint()) == 0.0 && distance(x.axisDirection(), y.axisDirection()) == 0.0
            && sameStoredCurve(*x.meridian(), *y.meridian());
    }
    if (a.type() != SurfaceType::BSpline) return false;
    const auto &x = static_cast<const BSplineSurface &>(a), &y = static_cast<const BSplineSurface &>(b);
    if (sameStoredSpline(x, y)) return true;
    const Interval u = x.uDomain(), v = x.vDomain(), nextU = y.uDomain(), nextV = y.vDomain();
    if (u.lo <= nextU.lo && u.hi >= nextU.hi && v.lo <= nextV.lo && v.hi >= nextV.hi
        && (u.lo != nextU.lo || u.hi != nextU.hi || v.lo != nextV.lo || v.hi != nextV.hi))
        return sameStoredSurface(b, a, tolerance);
    if (x.uDegree() != y.uDegree() || x.vDegree() != y.vDegree()
        || nextU.lo > u.lo || nextU.hi < u.hi || nextV.lo > v.lo || nextV.hi < v.hi
        || (u.lo == nextU.lo && u.hi == nextU.hi && v.lo == nextV.lo && v.hi == nextV.hi)) return false;
    try {
        return sameStoredSpline(extendBSplineSurface(x, nextU, nextV), y);
    } catch (const std::domain_error &) {
        return false;
    }
}

}
#endif
