#pragma once
#include "fk_bspline_surface.h"
#include "fk_topology.h"
#include <functional>
namespace ForgeCad::Kernel::detail {
struct BlendCut {
    CurvePtr<3> curve;
    Interval range;
    bool forward = true; // dal punto `from` a `to` il parametro cresce
    double gap = 0.0;
    // Nonzero only for the affine ISO branch proved on both carriers.
    // These ORIGINAL UV maps use the emitted edge parameter, irrespective of fin sense.
    CurvePtr<2> pcurveS, pcurveT;
    double certificateBudget = 0.0;
    Interval sV{0.0, 0.0}; // traceBetween: valori di v su S toccati dai nodi
};
// Each affine ISO certificate belongs to a planned edge and one original
// blend carrier. Pointer identity is construction provenance, never proximity.
struct BlendMitreUse {
    int edge;
    CurvePtr<3> curve;
    SurfacePtr surface;
    CurvePtr<2> pcurve;
    Interval range;
    double budget;
};
// Uses the SAME original edge parameter on both fin senses. Reversing the
// carrier changes only V through the original carrier domain reflection.
CurvePtr<2> originalBlendMitreUV(const CurvePtr<2> &pcurve, bool reversed, const Interval &vDomain);
// Installed by planned edge index + original carrier identity before assembly;
// actual output is checked again by identity and ORIGINAL coefficient proofs.
void installBlendMitreCertificates(std::vector<Body::BuildFace> &faces, const std::vector<BlendMitreUse> &plans);
void requireEmittedBlendMitres(const Body &body, const std::vector<BlendMitreUse> &plans);
struct BlendContactJet {
    Vec3 point, derivative;
};
using BlendContactFunction = std::function<BlendContactJet(double)>;
// Newton smorzato sui contatti: non cambia il residuo ammesso dal chiamante.
void meetBlendContacts(const BlendContactFunction &a, const BlendContactFunction &b, double size, double scale, double &ta, double &tb);
std::shared_ptr<const BSplineSurface> orientBlendMitre(const std::shared_ptr<const BSplineSurface> &surface, bool reverse);
// Rebuilds the existing composition provider on actual controls/carrier/UV.
// Throws on unsupported representation, incomplete coverage or failed bound.
double requireBlendMitreCertificate(const CurvePtr<3> &curve, const SurfacePtr &surface,
                                    const CurvePtr<2> &pcurve, const Interval &range, double budget);
BlendCut traceBlendMitre(const Surface &s, const Surface &t, double t0, double s0, const double endpoint[4], double scale,
                         double fitTolerance);
} // namespace ForgeCad::Kernel::detail
