#include "fk_precision.h"
#ifndef FORGECAD_FK_HELIX_H
#define FORGECAD_FK_HELIX_H

#include "fk_bspline.h"

// Eliche cilindriche, coniche e su fasce di rivoluzione. Spirali piane di Archimede.
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
    // Stored IEEE coefficients used by evaluate(), without recomputing the
    // divisions or tangent from Spec when constructing a certificate.
    double radialSlope() const { return slope_; }
    double risePerRadian() const { return rise_; }
    // Lunghezza esatta (forma chiusa per l'elica cilindrica, quadratura negli altri casi).
    double length() const;

private:
    HelixSpec spec_;
    double slope_ = 0.0;   // dr/dt
    double rise_ = 0.0;    // dz/dt
    double sense_ = 1.0;
};

// Approssimazione B-spline (grado 5, nodi tripli: C2) entro `tolerance` dalla curva esatta.
BSplineCurve<3> helixBSpline(const HelixCurve &helix, double tolerance = scaledAccuracy(1e-9));

class Body;
template <class Tag> struct Id;
struct FaceTag;

struct SurfaceHelixSpec {
    double pitchOrTurns;
    bool byTurns;
    double startAngle; // radianti
    bool leftHanded;
    bool reverse;
};

// Distanza assiale tra i due anelli del bordo della faccia.
double revolutionFaceHelixHeight(const Body &body, Id<FaceTag> face);

// Raggio dal meridiano alla quota descritta dal passo, su tutta l'altezza della faccia.
// Il passo o il numero di giri dipende da quale dato è imposto. La faccia deve essere
// una fascia di rivoluzione completa con meridiano monotono lungo l'asse.
// Il dominio della curva risultante è l'angolo percorso, in radianti.
BSplineCurve<3> revolutionFaceHelix(const Body &body, Id<FaceTag> face, const SurfaceHelixSpec &spec,
                                     double tolerance = linearResolution());

}

#endif
