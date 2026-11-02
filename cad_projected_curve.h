#ifndef FORGECAD_PROJECTED_CURVE_H
#define FORGECAD_PROJECTED_CURVE_H

#include "cad_types.h"

namespace ForgeCad {

// ProjectedCurve: projectionMode 0 = schizzo sul corpo (storico),
// projectionMode 1 = due schizzi. ruledFirst/ruledSecond sono le liste
// persistenti dei riferimenti kind 7; le altre feature conservano i loro usi.
QVector<ForgeCurve> forgeSketchProjectedCurves(const ExtrusionObject &definition, int owner,
    const QVector<SketchObject> &sketches, const QVector<ExtrusionObject> &bodies,
    QString *error, QString *summary = nullptr);

}

#endif
