#ifndef FORGECAD_FK_OFFSET_SUPPORT_H
#define FORGECAD_FK_OFFSET_SUPPORT_H
#include "fk_bspline_surface.h"
namespace ForgeCad::Kernel::detail {
struct RawModel;
struct RawFace;
// Conserva il dominio originale e aggiunge pezze razionali C2. Il polinomio
// omogeneo quadratico terminale evita l'amplificazione delle campate minute.
BSplineSurface continueOffsetSupport(const BSplineSurface &source, const Interval &u, const Interval &v);
bool regularOffset(const Surface &surface, double distance, const Vec2 &uv);
// Il raggio di curvatura si verifica sul trim effettivamente conservato:
// le pieghe che il rifilo elimina non invalidano l'intera finestra rettangolare.
void requireRegularOffsetTrim(const RawModel &model, const RawFace &face, double distance, const Interval &u, const Interval &v,
                              double tolerance);
} // namespace ForgeCad::Kernel::detail
#endif
