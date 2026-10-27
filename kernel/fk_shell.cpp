#include "fk_shell.h"
#include "fk_history_tools.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

#include "fk_boolean.h"
#include "fk_offset.h"
#include "fk_precision.h"
#include "fk_sew.h"

namespace ForgeCad::Kernel {

Body shellBody(const Body &solid, const std::vector<FaceId> &removed, double thickness, BodyHistory *history) {
    if (history)
        *history = {};
    if (solid.isSheet() || solid.faces().empty())
        throw std::domain_error("guscio: serve un solido");
    if (!(thickness > kLinearResolution) || !std::isfinite(thickness))
        throw std::domain_error("guscio: lo spessore deve essere positivo e finito");
    std::set<int> openings;
    for (FaceId f : removed) {
        if (!solid.contains(f))
            throw std::invalid_argument("guscio: faccia dell'apertura assente");
        openings.insert(f.index);
    }
    if (openings.size() == solid.faces().size())
        throw std::domain_error("guscio: non resta nessuna faccia");

    // La cavita' conserva i supporti dell'esterno. Le facce dell'apertura
    // restano alla quota originale; le altre si spostano verso l'interno.
    // I raccordi analitici il cui raggio residuo e' nullo entro la risoluzione
    // si contraggono: i supporti adiacenti si prolungano fino a intersecarsi.
    std::vector<FaceOffset> offsets;
    for (FaceId f : solid.faces()) {
        const auto &face = solid.face(f);
        const auto &surface = *face.surface;
        const bool opening = openings.count(f.index) != 0;
        double radius = 0.0;
        if (surface.type() == SurfaceType::Cylinder)
            radius = static_cast<const CylindricalSurface &>(surface).radius();
        else if (surface.type() == SurfaceType::Sphere)
            radius = static_cast<const SphericalSurface &>(surface).radius();
        else if (surface.type() == SurfaceType::Torus)
            radius = static_cast<const ToroidalSurface &>(surface).minorRadius();
        const bool collapse = !opening && face.sense && radius > 0.0 && radius - thickness <= kLinearResolution;
        offsets.push_back({f, opening ? 0.0 : -thickness, collapse});
    }
    const auto offset = offsetJoinedFaces(solid, offsets, kLinearResolution);
    BodyHistory sewn;
    auto inner = sewSheets({&offset.body}, kBooleanConnectionTolerance, true, history ? &sewn : nullptr);
    if (!inner.solid)
        throw std::domain_error("guscio: la superficie interna non si chiude");
    BodyHistory cut;
    auto result = booleanOperation(solid, inner.body, BooleanOperation::Subtract, {}, history ? &cut : nullptr);
    if (history) {
        auto inside = detail::transportHistory(sewn, {&offset.history});
        auto original = detail::indexHistory(solid, solid, HistoryRelation::Same);
        *history = detail::transportHistory(cut, {&original, &inside});
    }
    return result;
}

} // namespace ForgeCad::Kernel
