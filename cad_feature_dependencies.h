#ifndef FORGECAD_FEATURE_DEPENDENCIES_H
#define FORGECAD_FEATURE_DEPENDENCIES_H

#include "cad_types.h"

namespace ForgeCad {

// Classificazione dei risultati e dei riferimenti, indipendente dal viewport.
bool hasGeometry(const ExtrusionObject &body);
bool isCurveBody(const ExtrusionObject &body);
bool isDatumBody(const ExtrusionObject &body);
bool isShapeBody(const ExtrusionObject &body);
bool mergingFeature(const ExtrusionObject &body);
bool isBodyRef(const GeometryRef &ref);
bool isSketchRef(const GeometryRef &ref);

// Indici dei corpi/schizzi necessari a costruire la feature. I riferimenti
// non validi restano nell'elenco: chi rigenera deve poterli segnalare.
QVector<int> bodyOperands(const ExtrusionObject &body);
QVector<int> sketchesOf(const ExtrusionObject &body);
bool usesSketch(const ExtrusionObject &body);

// Operandi consumati dalla feature; distinti dalle semplici dipendenze.
QVector<int> hiddenOperands(const ExtrusionObject &body);
// Aggregato legacy rappresentato da risultati derivati nella storia corrente.
bool isExposedPatternAggregate(int index, const QVector<ExtrusionObject> &bodies, int end = -1);
// Include i risultati esposti da un aggregato storico consumato dalla feature.
QVector<int> hiddenOperands(const ExtrusionObject &body, const QVector<ExtrusionObject> &bodies);

}

#endif
