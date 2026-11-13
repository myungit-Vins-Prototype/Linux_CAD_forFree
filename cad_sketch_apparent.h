#ifndef FORGECAD_SKETCH_APPARENT_H
#define FORGECAD_SKETCH_APPARENT_H
#include "cad_types.h"
namespace ForgeCad {
// Formato PRT69: faccia sorgente normale, con ruolo di contorno nello schizzo.
// element.element identifica il lato analitico (-1/+1, 0 per la sfera).
inline constexpr int kSketchApparentFace = 3;
inline bool isSketchApparentReference(const GeometryRef &ref) {
    return ref.kind == 5 && ref.element.kind == kSketchApparentFace;
}
}
#endif
