#ifndef FORGECAD_FK_HELIX_H
#define FORGECAD_FK_HELIX_H

#include "fk_bspline.h"

// Eliche (cilindriche e coniche) e spirali piane (di Archimede).
//
// Nel sistema `frame` (asse = Z, angolo 0 lungo X), con t = angolo percorso in
// [0, 2 pi giri] e s = +1 (destrorsa: antioraria attorno a Z) o -1:
//   theta(t) = startAngle + s t,
//   elica:   r(t) = radius + z(t) tan(taper),  z(t) = pitch t / (2 pi),
//   spirale: r(t) = radius + pitch t / (2 pi),  z = 0,
//   P(t) = O + r(t) (cos theta X + sin theta Y) + z(t) Z.
// Il passo dell'elica e' l'avanzamento lungo l'asse per giro (costante anche
// sull'elica conica, che e' una retta nello spazio (u, v) del cono), quello
// della spirale la crescita del raggio per giro.
//
// HelixCurve e' la curva esatta (derivate di ogni ordine in forma chiusa); non
// e' un tipo di curva degli edge (CurveType::Other): serve da percorso esatto
// allo sweep. Gli edge e OCCT usano helixBSpline, la sua approssimazione
// B-spline di grado 5 (C2) entro la tolleranza.
namespace ForgeCad::Kernel {

struct HelixSpec {
    Frame3 frame;
    double radius = 1.0;
    double pitch = 1.0;
    double turns = 1.0;
    double taper = 0.0;  // radianti (elica conica), |taper| < pi / 2
    bool spiral = false;
    bool leftHanded = false;
    double startAngle = 0.0;  // radianti
};

class HelixCurve final : public Curve<3> {
public:
    // std::invalid_argument se i parametri non sono validi (passo o giri non
    // positivi, raggio negativo o che si annulla prima della fine).
    explicit HelixCurve(const HelixSpec &spec);

    CurveType type() const override { return CurveType::Other; }
    Interval domain() const override { return {0.0, kTwoPi * spec_.turns}; }
    void evaluate(double t, int order, Vec3 *out) const override;

    const HelixSpec &spec() const { return spec_; }
    double radiusAt(double t) const { return spec_.radius + slope_ * t; }
    // Lunghezza esatta (forma chiusa per l'elica cilindrica, quadratura negli altri casi).
    double length() const;

private:
    HelixSpec spec_;
    double slope_ = 0.0;   // dr/dt
    double rise_ = 0.0;    // dz/dt
    double sense_ = 1.0;
};

// Approssimazione B-spline (grado 5, nodi tripli: C2) entro `tolerance` dalla curva esatta.
BSplineCurve<3> helixBSpline(const HelixCurve &helix, double tolerance = 1e-9);

}

#endif
