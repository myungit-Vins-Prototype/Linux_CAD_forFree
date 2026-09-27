#ifndef FORGECAD_CURVE_SOLVER_H
#define FORGECAD_CURVE_SOLVER_H

#include <QVector>
#include <vector>

#include "cad_types.h"
#include "fk_profile.h"

namespace ForgeCad {

// Geometria esatta (kernel proprio, double) di una curva di schizzo nel piano
// di schizzo: ogni tratto e' una curva con il suo intervallo. Spline = Bezier
// cubiche C1 a tratti come B-spline di grado 3 (nodi interni tripli); NURBS
// (poli + pesi, nodi uniformi clamped, grado <= 3); cerchio (parametro =
// angolo da +X); arco (centro, inizio, fine in senso antiorario); ellisse
// (semiasse maggiore lungo X del suo sistema); rettangoli e poligono come
// segmenti (parametro = lunghezza d'arco), uno per lato.
// Vuoto se i parametri non definiscono una curva valida.
std::vector<Kernel::ProfileSegment> curveGeometry(const CurveObject &curve);

// Maniglie tangenti iniziali (solo spline): 1/3 della corda tra i vicini.
void initializeTangentHandles(CurveObject &curve);

// Punti di visualizzazione di un tratto: suddivisione finche' la corda si
// scosta dalla curva meno di `deflection` e la tangente gira meno di `angular`.
void sampleCurve(const Kernel::Curve<2> &curve, const Kernel::Interval &range, double angular, double deflection, QVector<QPointF> &out);

// Aggiorna la geometria di visualizzazione `samples` dalla curva esatta.
// quality 0/1/2 = bassa/media/alta densita' di campionamento.
void recalculateCurve(CurveObject &curve, int quality = 1);

}

#endif
