#ifndef FORGECAD_FK_SKETCH_PROJECTION_H
#define FORGECAD_FK_SKETCH_PROJECTION_H

#include "fk_curve.h"
#include <string>
#include "fk_intersect.h"

namespace ForgeCad::Kernel {

struct SketchProjectionSpan {
    CurvePtr<3> curve;
    Interval range;
    std::string identity; // sorgente persistente opzionale, non dipende dall'ordine degli input
    SketchProjectionSpan(CurvePtr<3> c, Interval r, std::string key = {}) : curve(std::move(c)), range(r), identity(std::move(key)) {}
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
    const std::vector<SketchProjectionSpan> &second, const Vec3 &secondNormal, std::vector<std::string> *identities = nullptr);

// True anche per wrapper Trimmed/Transformed: il chiamante deve rifiutare
// operazioni che sostituirebbero l'intersezione implicita con un fit NURBS.
bool isImplicitSketchProjection(const Curve<3> &curve);

// Intersezioni esatte dell'intersezione implicita con un piano: si riducono
// a curve razionali nello stesso piano e si isolano tutte le radici.
PlaneRoots<3> sketchProjectionPlaneRoots(const Curve<3> &curve, const Interval &range,
    const Vec3 &normal, double offset, double tolerance);

// Immagine affine esatta usata dai riferimenti associativi degli schizzi.
// Conserva la curva 3D e il parametro: nessun fit NURBS o campione geometrico.
class SketchProjectionImage final : public Curve<2> {
public:
    SketchProjectionImage(CurvePtr<3> source, Interval range, Frame3 plane);
    CurveType type() const override { return CurveType::Other; }
    Interval domain() const override { return range_; }
    void evaluate(double t, int order, Vec2 *out) const override;
    void evaluateLeft(double t, int order, Vec2 *out) const override;
    std::vector<double> breakpoints(const Interval &range) const override { return source_->breakpoints(range); }
    const CurvePtr<3> &source() const { return source_; }
    const Frame3 &plane() const { return plane_; }
private:
    CurvePtr<3> source_;
    Interval range_;
    Frame3 plane_;
};

PlaneRoots<2> sketchProjectionImageRoots(const SketchProjectionImage &curve, const Interval &range,
    const Vec2 &normal, double offset, double tolerance);
CurvePtr<3> embedSketchProjectionImage(const SketchProjectionImage &curve, const Frame3 &frame, double z = 0.0);

}

#endif
