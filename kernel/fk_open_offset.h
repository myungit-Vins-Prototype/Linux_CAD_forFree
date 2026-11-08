#ifndef FORGECAD_FK_OPEN_OFFSET_H
#define FORGECAD_FK_OPEN_OFFSET_H

#include "fk_offset.h"

// Offset di lamine con spigoli vivi e aperture planari: chiusure temporanee
// esatte, intersezioni dei supporti sul trim necessario, rimozione delle gole
// cilindriche di larghezza nulla e pareti anulari. Nessun cappuccio temporaneo
// resta nel risultato; genealogia esplicita verso le facce della lamina.
namespace ForgeCad::Kernel::detail {
bool canOffsetOpenSheet(const Body &sheet, double tolerance);
OffsetResult offsetOpenSheet(const Body &sheet, double distance, double tolerance);
Body thickenOpenSheet(const Body &sheet, double lower, double upper, double tolerance, BodyHistory *history);
} // namespace ForgeCad::Kernel::detail
#endif
