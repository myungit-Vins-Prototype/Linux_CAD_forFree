#ifndef FORGECAD_FK_OPEN_OFFSET_H
#define FORGECAD_FK_OPEN_OFFSET_H

#include "fk_offset.h"
#include <optional>

// Offset di lamine con spigoli vivi e aperture planari: chiusure temporanee
// esatte, intersezioni dei supporti sul trim necessario, rimozione delle gole
// cilindriche di larghezza nulla e pareti anulari. Nessun cappuccio temporaneo
// resta nel risultato; genealogia esplicita verso le facce della lamina.
namespace ForgeCad::Kernel::detail {
bool canOffsetOpenSheet(const Body &sheet, double tolerance);
OffsetResult offsetOpenSheet(const Body &sheet, double distance, double tolerance);

// Verifica la corrispondenza UV del contorno con la pelle a distanza.
// Usa un limite continuo per rette e spline UV anche razionali su
// carrier compatibili. Nelle altre famiglie usa estremi e campioni interni.
void requireOpeningOffset(const Surface &source, const Surface &image, const Curve<2> &pcurve,
                          Interval range, double signedDistance, double budget);
// Also supports bounded internal holes on a single B-spline carrier.
// An empty result means the open-sheet topology is outside the supported family.
std::optional<Body> tryThickenOpenSheet(const Body &sheet, double lower, double upper, double tolerance, BodyHistory *history);
} // namespace ForgeCad::Kernel::detail
#endif
