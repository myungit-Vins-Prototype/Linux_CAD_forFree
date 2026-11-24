#ifndef FORGECAD_FK_OFFSET_SUPPORT_H
#define FORGECAD_FK_OFFSET_SUPPORT_H
#include "fk_bspline_surface.h"
#include "fk_topology.h"
#include <array>
#include <optional>
namespace ForgeCad::Kernel::detail {
struct CollapsedPlanarBlend {
    EdgeId planarRail, oppositeRail;
    std::array<EdgeId, 2> transverse;
    double distance;
};
// Certifica il collasso a una curva di una fascia NURBS con pcurve rettilinee
// e un bordo longitudinale su piano. Lo spessore usa il verso orientato della
// faccia. Il chiamante verifica raccordi adiacenti e ricucitura dei bordi.
std::optional<CollapsedPlanarBlend> collapsedPlanarBlend(const Body &body, FaceId id, double distance, double tolerance);
// Stima lo spessore critico nel verso richiesto e certifica il collasso
// su tutte le pezze prima di riconoscere il superamento del raggio.
std::optional<CollapsedPlanarBlend> overrunPlanarBlend(const Body &body, FaceId id, double distance, double tolerance);
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
