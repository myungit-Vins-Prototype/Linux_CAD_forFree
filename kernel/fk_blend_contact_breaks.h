#ifndef FORGECAD_FK_BLEND_CONTACT_BREAKS_H
#define FORGECAD_FK_BLEND_CONTACT_BREAKS_H

#include <array>
#include <functional>
#include <vector>
#include "fk_surface.h"

// Interno al fit dei raccordi: eventi nel parametro dello spigolo quando
// i contatti attraversano nodi degli appoggi con derivate non C2.
namespace ForgeCad::Kernel::detail {

struct BlendContactSample {
    std::array<double, 4> uv;          // uA, vA, uB, vB
    std::array<double, 4> derivative;  // rispetto al parametro dello spigolo
};

std::vector<double> blendContactBreaks(const Surface &a, const Surface &b,
    const Interval &range, const std::vector<double> &edgeBreaks,
    const std::function<BlendContactSample(double)> &sample);

}
#endif
