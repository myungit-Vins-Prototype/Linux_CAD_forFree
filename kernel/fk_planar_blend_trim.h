#ifndef FORGECAD_FK_PLANAR_BLEND_TRIM_H
#define FORGECAD_FK_PLANAR_BLEND_TRIM_H

#include "fk_offset.h"

namespace ForgeCad::Kernel::detail {

struct PlanarBlendCollapse {
    std::vector<FaceId> faces;
    std::vector<std::pair<EdgeId, EdgeId>> rails;
};

PlanarBlendCollapse findPlanarBlendCollapse(const Body &sheet, double distance);

struct PlanarBlendTrim {
    FaceId support;
    std::vector<FaceId> faces;
    std::vector<EdgeId> rails;
};

// Riconosce una fascia chiusa consumata dall'offset, appoggiata a un
// singolo contorno planare interno. I bordi esposti restano identificati.
PlanarBlendTrim findPlanarBlendTrim(const Body &sheet, double distance);

OffsetResult offsetWithPlanarBlendTrim(const Body &sheet, const PlanarBlendTrim &trim, double distance,
                                       double accuracy, double tolerance, double joinAngle);

}
#endif
