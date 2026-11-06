#pragma once
#include "fk_boolean.h"
namespace ForgeCad::Kernel {
// Conserva tutti i volumi sul lato scelto di una faccia orientata. La faccia
// finita deve attraversare completamente le parti tagliate: nessuna estensione
// implicita. Le nuove facce di chiusura conservano il carrier dello strumento.
Body clipSolidByFace(const Body &solid, const Body &face, bool positive,
                     const BooleanOptions &options = {}, BodyHistory *history = nullptr);
// Versi automatici verso l'altra faccia; le inversioni sono esplicite.
Body trimSolidBetweenFaces(const Body &solid, const Body &first, const Body &second,
                          bool reverseFirst = false, bool reverseSecond = false,
                          const BooleanOptions &options = {}, BodyHistory *history = nullptr);
}
