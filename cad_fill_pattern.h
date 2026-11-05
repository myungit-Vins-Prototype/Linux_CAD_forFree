#pragma once
#include "cad_types.h"
#include "fk_math.h"
namespace ForgeCad {
// Tipi3rettangolare,4sfalsato,5esagonale,6concentrico. Contorno associativo
// in refs[0]; il rettangolo conservativo della sorgente deve stare nel
// materiale, senza attraversare fori o bordi. Nessuna mesh di calcolo.
bool fillPatternPlacements(const PatternParameters &parameters, int source, int owner,
    const QVector<SketchObject> &sketches, const QVector<ExtrusionObject> &bodies,
    std::vector<Kernel::Transform3> &placements, QString *error);
}
