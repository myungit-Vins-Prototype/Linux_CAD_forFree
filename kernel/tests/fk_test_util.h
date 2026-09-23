#ifndef FORGECAD_FK_TEST_UTIL_H
#define FORGECAD_FK_TEST_UTIL_H

// Ponte verso OpenCASCADE, usato come riferimento, e generatori casuali
// riproducibili (seme fisso).

#include <Geom2d_BSplineCurve.hxx>
#include <Geom_BSplineCurve.hxx>
#include <Geom_BSplineSurface.hxx>
#include <TColStd_Array1OfInteger.hxx>
#include <TColStd_Array1OfReal.hxx>
#include <TColStd_Array2OfReal.hxx>
#include <TColgp_Array1OfPnt.hxx>
#include <TColgp_Array1OfPnt2d.hxx>
#include <TColgp_Array2OfPnt.hxx>
#include <gp_Ax3.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Pnt2d.hxx>
#include <gp_Vec.hxx>
#include <gp_Vec2d.hxx>

#include <algorithm>
#include <random>

#include "fk_bspline.h"
#include "fk_bspline_surface.h"
#include "fk_test.h"

namespace fktest {

using namespace ForgeCad::Kernel;

inline gp_Pnt toPnt(const Vec3 &v) { return gp_Pnt(v.x(), v.y(), v.z()); }
inline gp_Dir toDir(const Vec3 &v) { return gp_Dir(v.x(), v.y(), v.z()); }
inline gp_Pnt2d toPnt(const Vec2 &v) { return gp_Pnt2d(v.x(), v.y()); }
inline Vec3 fromOcct(const gp_Pnt &p) { return Vec3(p.X(), p.Y(), p.Z()); }
inline Vec3 fromOcct(const gp_Vec &v) { return Vec3(v.X(), v.Y(), v.Z()); }
inline Vec2 fromOcct(const gp_Pnt2d &p) { return Vec2(p.X(), p.Y()); }
inline Vec2 fromOcct(const gp_Vec2d &v) { return Vec2(v.X(), v.Y()); }

// Vicinanza relativa: |a - b| <= tolerance * max(1, |b|).
template <int N>
bool near(const Vec<N> &a, const Vec<N> &b, double tolerance) {
    return distance(a, b) <= tolerance * std::max(1.0, norm(b));
}

inline void compressKnots(const std::vector<double> &knots, TColStd_Array1OfReal &distinct, TColStd_Array1OfInteger &multiplicities) {
    std::vector<double> values;
    std::vector<int> counts;
    for (double knot : knots) {
        if (!values.empty() && values.back() == knot) ++counts.back();
        else { values.push_back(knot); counts.push_back(1); }
    }
    distinct.Resize(1, int(values.size()), false);
    multiplicities.Resize(1, int(values.size()), false);
    for (std::size_t i = 0; i < values.size(); ++i) {
        distinct.SetValue(int(i) + 1, values[i]);
        multiplicities.SetValue(int(i) + 1, counts[i]);
    }
}

inline Handle(Geom_BSplineCurve) toOcct(const BSplineCurve<3> &curve) {
    TColgp_Array1OfPnt poles(1, curve.poleCount());
    TColStd_Array1OfReal weights(1, curve.poleCount());
    for (int i = 0; i < curve.poleCount(); ++i) {
        poles.SetValue(i + 1, toPnt(curve.poles()[i]));
        weights.SetValue(i + 1, curve.weight(i));
    }
    TColStd_Array1OfReal knots(1, 1);
    TColStd_Array1OfInteger multiplicities(1, 1);
    compressKnots(curve.knots(), knots, multiplicities);
    return new Geom_BSplineCurve(poles, weights, knots, multiplicities, curve.degree());
}

inline Handle(Geom2d_BSplineCurve) toOcct(const BSplineCurve<2> &curve) {
    TColgp_Array1OfPnt2d poles(1, curve.poleCount());
    TColStd_Array1OfReal weights(1, curve.poleCount());
    for (int i = 0; i < curve.poleCount(); ++i) {
        poles.SetValue(i + 1, toPnt(curve.poles()[i]));
        weights.SetValue(i + 1, curve.weight(i));
    }
    TColStd_Array1OfReal knots(1, 1);
    TColStd_Array1OfInteger multiplicities(1, 1);
    compressKnots(curve.knots(), knots, multiplicities);
    return new Geom2d_BSplineCurve(poles, weights, knots, multiplicities, curve.degree());
}

inline double uniform(std::mt19937 &rng, double lo, double hi) {
    return std::uniform_real_distribution<double>(lo, hi)(rng);
}
inline int uniformInt(std::mt19937 &rng, int lo, int hi) {
    return std::uniform_int_distribution<int>(lo, hi)(rng);
}

template <int N>
Vec<N> randomVec(std::mt19937 &rng, double extent) {
    Vec<N> result;
    for (int i = 0; i < N; ++i) result[i] = uniform(rng, -extent, extent);
    return result;
}

inline Vec3 randomDirection(std::mt19937 &rng) {
    for (;;) {
        const Vec3 v = randomVec<3>(rng, 1.0);
        const double length = norm(v);
        if (length > 0.1 && length <= 1.0) return v / length;
    }
}

inline Frame3 randomFrame(std::mt19937 &rng, double extent = 100.0) {
    return Frame3(randomVec<3>(rng, extent), randomDirection(rng), randomDirection(rng));
}

inline gp_Ax3 toAx3(const Frame3 &frame) {
    return gp_Ax3(toPnt(frame.origin()), toDir(frame.zDir()), toDir(frame.xDir()));
}

// Vettore dei nodi clamped casuale: nodi interni su una griglia (niente
// tratti minuscoli) con molteplicita' fino al grado, dominio scalato e traslato.
inline std::vector<double> randomKnots(std::mt19937 &rng, int degree, int poleCount) {
    int remaining = poleCount - degree - 1;
    std::vector<int> used;
    std::vector<double> interior;
    while (remaining > 0) {
        int slot;
        do slot = uniformInt(rng, 1, 63); while (std::find(used.begin(), used.end(), slot) != used.end());
        used.push_back(slot);
        const int multiplicity = uniformInt(rng, 1, std::min(degree, remaining));
        interior.insert(interior.end(), multiplicity, slot / 64.0);
        remaining -= multiplicity;
    }
    std::sort(interior.begin(), interior.end());
    const double offset = uniform(rng, -5.0, 5.0);
    const double scale = uniform(rng, 0.5, 3.0);
    std::vector<double> knots(degree + 1, 0.0);
    knots.insert(knots.end(), interior.begin(), interior.end());
    knots.insert(knots.end(), degree + 1, 1.0);
    for (double &knot : knots) knot = offset + scale * knot;
    return knots;
}

// B-spline clamped casuale di grado 1..5.
template <int N>
BSplineCurve<N> randomBSpline(std::mt19937 &rng, bool rational) {
    const int degree = uniformInt(rng, 1, 5);
    const int poleCount = degree + 1 + uniformInt(rng, 0, 8);
    std::vector<double> knots = randomKnots(rng, degree, poleCount);
    std::vector<Vec<N>> poles(poleCount);
    for (Vec<N> &pole : poles) pole = randomVec<N>(rng, 100.0);
    std::vector<double> weights;
    if (rational)
        for (int i = 0; i < poleCount; ++i) weights.push_back(uniform(rng, 0.3, 3.0));
    return BSplineCurve<N>(degree, std::move(knots), std::move(poles), std::move(weights));
}

// Superficie casuale: poli su una griglia regolare perturbata (una "lamiera"
// ondulata, non un groviglio), gradi 1..4.
inline BSplineSurface randomBSplineSurface(std::mt19937 &rng, bool rational, double noise = 15.0) {
    const int uDegree = uniformInt(rng, 1, 4), vDegree = uniformInt(rng, 1, 4);
    const int uCount = uDegree + 1 + uniformInt(rng, 0, 5), vCount = vDegree + 1 + uniformInt(rng, 0, 5);
    std::vector<Vec3> poles;
    std::vector<double> weights;
    for (int i = 0; i < uCount; ++i)
        for (int j = 0; j < vCount; ++j) {
            poles.push_back(Vec3(20.0 * i, 20.0 * j, 0.0) + randomVec<3>(rng, noise));
            if (rational) weights.push_back(uniform(rng, 0.3, 3.0));
        }
    return BSplineSurface(uDegree, vDegree, randomKnots(rng, uDegree, uCount), randomKnots(rng, vDegree, vCount),
                          uCount, vCount, std::move(poles), std::move(weights));
}

inline Handle(Geom_BSplineSurface) toOcct(const BSplineSurface &surface) {
    TColgp_Array2OfPnt poles(1, surface.uPoleCount(), 1, surface.vPoleCount());
    TColStd_Array2OfReal weights(1, surface.uPoleCount(), 1, surface.vPoleCount());
    for (int i = 0; i < surface.uPoleCount(); ++i)
        for (int j = 0; j < surface.vPoleCount(); ++j) {
            poles.SetValue(i + 1, j + 1, toPnt(surface.pole(i, j)));
            weights.SetValue(i + 1, j + 1, surface.weight(i, j));
        }
    TColStd_Array1OfReal uKnots(1, 1), vKnots(1, 1);
    TColStd_Array1OfInteger uMults(1, 1), vMults(1, 1);
    compressKnots(surface.uKnots(), uKnots, uMults);
    compressKnots(surface.vKnots(), vKnots, vMults);
    return new Geom_BSplineSurface(poles, weights, uKnots, vKnots, uMults, vMults, surface.uDegree(), surface.vDegree());
}

}

#endif
