#pragma once
#include "fk_quantize.h"

namespace ForgeCad::Kernel {

// Conserva vertici, incidenza e bordi esterni del B-rep di partenza.
std::size_t fitGyroidFaces(Body &body, const std::vector<FaceId> &candidates,
                           const SheetGyroid &pattern, double samplingPitch);

}
