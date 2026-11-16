#ifndef FORGECAD_ELLIPSE_H
#define FORGECAD_ELLIPSE_H

#include "cad_types.h"

namespace ForgeCad {
// Centro e vettori dei semiassi; per EllipseExtremes il centro deriva da A0/A1.
// Rifiuta coordinate non finite, assi degeneri e quattro estremi incoerenti.
bool ellipseAxes(const CurveObject &curve, QPointF &center, QPointF &axisA, QPointF &axisB);
// Tre clic: A0, A1, B0 proiettato sulla perpendicolare passante per il centro.
// Un risultato senza punti indica una costruzione degenere o non finita.
CurveObject ellipseFromExtremes(const QPointF &a0, const QPointF &a1, const QPointF &through);
// Chiamare dopo avere cambiato controlPoints[moved]. Conserva il controestremo
// dello stesso asse e la lunghezza dell'altro semiasse; un drag non valido
// ripristina previous. Non applicare dopo il solver, che conserva i vincoli.
void normalizeEllipseExtremes(CurveObject &curve, int moved, const QPointF &previous);
}

#endif
