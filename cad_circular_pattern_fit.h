#ifndef FORGECAD_CIRCULAR_PATTERN_FIT_H
#define FORGECAD_CIRCULAR_PATTERN_FIT_H

#include "cad_types.h"
#include "fk_math.h"

namespace ForgeCad {

// Calcolo esplicito delle istanze totali (originale compreso) di una lamina.
// Cerca due bordi liberi che una rotazione di 360/N sovrappone per intero.
// Verifica conservativa sui Bezier razionali esatti, senza mesh o fit.
// Non modifica geometria o tolleranze; false se la coincidenza non e' provata.
// La cucitura delle istanze resta un'operazione separata.
bool fitCircularPatternBorders(const ForgeBody &source, const Kernel::Vec3 &origin, const Kernel::Vec3 &axis,
                               int &count, double &maximumGap, QString *error);

}

#endif
