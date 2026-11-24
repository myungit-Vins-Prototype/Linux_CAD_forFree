#ifndef FORGECAD_FK_TPMS_FIELD_H
#define FORGECAD_FK_TPMS_FIELD_H

#include "fk_math.h"

namespace ForgeCad::Kernel {

struct TpmsPhase {
    double sine, cosine;
};

inline TpmsPhase tpmsPhase(double coordinate, double origin, double period) {
    const double angle = kTwoPi * (std::remainder(coordinate - origin, period) / period);
    return {std::sin(angle), std::cos(angle)};
}

inline double tpmsFieldFromPhases(TpmsPhase x, TpmsPhase y, TpmsPhase z,
                                                        double bandScale, double distanceScale) {
    const double value = x.sine * y.cosine + y.sine * z.cosine + z.sine * x.cosine;
    const double gx = x.cosine * y.cosine - z.sine * x.sine;
    const double gy = y.cosine * z.cosine - x.sine * y.sine;
    const double gz = z.cosine * x.cosine - y.sine * z.sine;
    const double gradient = std::sqrt(gx * gx + gy * gy + gz * gz);
    return (bandScale * gradient - std::fabs(value)) * distanceScale;
}

}

#endif
