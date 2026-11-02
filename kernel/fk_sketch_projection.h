#ifndef FORGECAD_FK_SKETCH_PROJECTION_H
#define FORGECAD_FK_SKETCH_PROJECTION_H

#include "fk_curve.h"

namespace ForgeCad::Kernel {

struct SketchProjectionSpan {
    CurvePtr<3> curve;
    Interval range;
};

// Intersezione delle superfici C_i(t) + lambda n_i, con lambda libero.
// Le sorgenti sono curve razionali esatte contenute nei rispettivi piani.
// I tratti vengono separati alle radici Bernstein della coordinata lungo
// n_1 x n_2: nessun accoppiamento arbitrario dei parametri, fit o mesh.
// Le componenti non razionali conservano gli input e risolvono una sola
// equazione scalare monotona a ogni valutazione; la topologia non dipende
// dai campioni di display. Le tangenze comuni non regolari sono rifiutate.
std::vector<CurvePtr<3>> projectSketchOnSketch(
    const std::vector<SketchProjectionSpan> &first, const Vec3 &firstNormal,
    const std::vector<SketchProjectionSpan> &second, const Vec3 &secondNormal);

// True anche per wrapper Trimmed/Transformed: il chiamante deve rifiutare
// operazioni che sostituirebbero l'intersezione implicita con un fit NURBS.
bool isImplicitSketchProjection(const Curve<3> &curve);

}

#endif
