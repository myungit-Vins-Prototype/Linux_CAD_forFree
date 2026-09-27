#ifndef FORGECAD_PATTERN_H
#define FORGECAD_PATTERN_H

#include <QString>
#include <QVector>
#include <vector>

#include "cad_types.h"
#include "fk_math.h"

// Ripetizione dei corpi e delle funzioni (BodyFeature::Pattern, vedi
// PatternParameters): movimenti esatti (traslazioni, rotazioni, simmetrie di
// fk_transform) e unione delle copie con le booleane del kernel.
namespace ForgeCad {

// Le trasformazioni delle istanze oltre all'originale (per lo specchio la
// simmetria), dai riferimenti risolti sulla geometria del corpo `owner`
// (i corpi dei riferimenti hanno indice minore).
bool patternPlacements(const PatternParameters &pattern, int owner, const QVector<SketchObject> &sketches, const QVector<ExtrusionObject> &bodies,
                       std::vector<Kernel::Transform3> &placements, QString *error);

// Il corpo con le sue copie (unione; senza l'originale se !keepOriginal).
ForgeBody forgePattern(const ForgeBody &base, const std::vector<Kernel::Transform3> &placements, bool keepOriginal, QString *error);

// Ripetizione della funzione: `target` unito (operation Union) o meno
// (Difference) lo strumento e tutte le sue copie.
ForgeBody forgePatternFeature(const ForgeBody &target, const ForgeBody &tool, BooleanOperation operation,
                              const std::vector<Kernel::Transform3> &placements, QString *error);

}

#endif
