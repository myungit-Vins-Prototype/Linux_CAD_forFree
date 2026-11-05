#pragma once
#include "cad_types.h"

namespace ForgeCad {
struct PatternParameter {
    PatternVariation reference;
    QString label, unit;
    double value = 0, minimum = -1e9, maximum = 1e9;
    bool integer = false;
};
// Parametri attivi nella sorgente e nelle sue dipendenze; identificatori persistenti.
QVector<PatternParameter> patternParameters(const ExtrusionObject &pattern, const QVector<SketchObject> &sketches,
                                            const QVector<ExtrusionObject> &bodies, QString *error = nullptr);
bool samePatternParameter(const PatternVariation &a, const PatternVariation &b);
// Rigenera copie isolate delle sole dipendenze modificate e le dispone con il passo originale.
ForgeBody forgeVariablePattern(const ExtrusionObject &pattern, int index, const QVector<SketchObject> &sketches,
                               const QVector<ExtrusionObject> &bodies, QString *error);
} // namespace ForgeCad
