#ifndef FORGECAD_FK_APPARENT_CONTOUR_H
#define FORGECAD_FK_APPARENT_CONTOUR_H

#include "fk_topology.h"
#include "fk_precision.h"

namespace ForgeCad::Kernel {

// Curve esatte della faccia con normale ortogonale alla direzione di
// proiezione. Non sono edge B-rep: side identifica il ramo analitico.
struct ApparentContour {
    CurvePtr<3> curve;
    Interval range;
    int side = 0;
};
// Cilindri e coni: generatrici; sfere: cerchio massimo; tori: meridiani
// nella vista laterale. Ritaglio sui loop B-rep, senza mesh o fit grafici.
// Per direzioni/superfici non supportate restituisce nessun contorno.
std::vector<ApparentContour> faceApparentContours(const Body &body, FaceId face,
    const Vec3 &direction, double tolerance = kBooleanConnectionTolerance);

}
#endif
