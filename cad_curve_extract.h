#ifndef FORGECAD_CURVE_EXTRACT_H
#define FORGECAD_CURVE_EXTRACT_H
#include "cad_types.h"
namespace ForgeCad {
// Estrazione associativa, senza proiezione della geometria risultante.
// cutHint sceglie l'intersezione piu' vicina lungo il dominio della sorgente.
QVector<ForgeCurve> extractCurves3D(const CurveExtractionParameters &parameters, int owner,
    const QVector<SketchObject> &sketches, const QVector<ExtrusionObject> &bodies,
    QString *error, QVector<double> *cuts = nullptr);
}
#endif
