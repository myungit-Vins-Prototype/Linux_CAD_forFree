#ifndef FORGECAD_FK_BLEND_POLAR_H
#define FORGECAD_FK_BLEND_POLAR_H

#include <optional>
#include "fk_topology.h"

namespace ForgeCad::Kernel {
class BodyHistory;
namespace detail {
// Ventaglio chiuso di facce a due bordi fra due poli: raccordo simultaneo
// di tutti i giunti, con calotte sferiche comuni e collassi G1 ai poli lisci.
// nullopt: topologia fuori da questo caso. Un caso riconosciuto ma non
// costruibile al raggio richiesto fallisce senza cambiare corpo o raggio.
std::optional<Body> blendPolarEdges(const Body &body, const std::vector<EdgeId> &selected,
                                   double radius, BodyHistory *history);
}
}
#endif
