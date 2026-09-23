#ifndef FORGECAD_CURVE_SOLVER_H
#define FORGECAD_CURVE_SOLVER_H

#include <Geom2d_Curve.hxx>
#include <QVector>

#include "cad_types.h"

namespace ForgeCad {

// Geometria esatta (OpenCASCADE, double) di una curva di schizzo nel piano
// di schizzo. Restituisce piu' tratti solo per il poligono (un segmento per lato).
// Vuoto se i parametri non definiscono una curva valida.
QVector<Handle(Geom2d_Curve)> curveGeometry(const CurveObject &curve);

// Maniglie tangenti iniziali (solo spline): 1/3 della corda tra i vicini.
void initializeTangentHandles(CurveObject &curve);

// Aggiorna la geometria di visualizzazione `samples` dalla curva esatta.
// quality 0/1/2 = bassa/media/alta densita' di campionamento.
void recalculateCurve(CurveObject &curve, int quality = 1);

}

#endif
