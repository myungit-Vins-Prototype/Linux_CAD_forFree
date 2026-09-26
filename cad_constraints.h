#ifndef FORGECAD_CONSTRAINTS_H
#define FORGECAD_CONSTRAINTS_H

#include <QPointF>
#include <QString>
#include <QVector>

#include "cad_types.h"

// Vincoli geometrici dello schizzo come oggetti (SketchObject::geometricConstraints)
// e il loro risolutore.
//
// Ogni vincolo e' un insieme di equazioni nelle coordinate dei punti dello
// schizzo (estremi dei segmenti, punti di controllo delle curve), in unita' di
// lunghezza. Il risolutore e' un Gauss-Newton a passo di norma minima
// (J^T (J J^T + mu I)^-1 r): i punti che nessun vincolo tocca non si muovono,
// gli altri si spostano il meno possibile. Le equazioni implicite delle curve
// (la fine di un arco sul suo cerchio, i semiassi dell'ellisse perpendicolari)
// valgono sempre. I bersagli (`PointTarget`: il punto trascinato, il valore
// scelto in una quota) sono equazioni in piu'. Tutto in double; la soluzione
// soddisfa i vincoli entro 1e-10 della dimensione dello schizzo.
namespace ForgeCad {

QString constraintName(ConstraintType type);
// Simbolo del vincolo accanto alle entita' (testo breve).
QString constraintSymbol(ConstraintType type);
bool isDimension(ConstraintType type);

// Descrizione di un riferimento ("Segmento 3", "estremo 1 del segmento 3",
// "centro del cerchio 2", "Origine"...) e del vincolo.
QString describeRef(const SketchObject &sketch, const ConstraintRef &ref);
QString describeConstraint(const SketchObject &sketch, const SketchConstraint &constraint);

// I vincoli che si possono dare ai riferimenti scelti (uno o due).
QVector<ConstraintType> applicableConstraints(const SketchObject &sketch, const QVector<ConstraintRef> &refs);

// Vincolo nuovo del tipo dato: le quote prendono la misura attuale, il
// vincolo fisso le posizioni attuali, la tangenza tra cerchi il verso attuale.
SketchConstraint makeConstraint(const SketchObject &sketch, ConstraintType type, const QVector<ConstraintRef> &refs);

// Misura attuale di una quota (lunghezze, angoli in gradi).
double currentMeasure(const SketchObject &sketch, const SketchConstraint &constraint);

// Scarto massimo delle equazioni del vincolo (0 se soddisfatto).
double constraintError(const SketchObject &sketch, const SketchConstraint &constraint);

// Il riferimento usa l'entita' (segmento o curva).
bool refersTo(const SketchConstraint &constraint, int kind, int element);

// Punti in cui disegnare il simbolo del vincolo, uno per riferimento che non
// sia del piano (coordinate dello schizzo), con la direzione dell'entita' li'.
struct ConstraintAnchor {
    QPointF point;
    QPointF direction;  // tangente dell'entita' (per spostare il simbolo di lato), nulla sui punti
    bool onPoint = false;
};
QVector<ConstraintAnchor> constraintAnchors(const SketchObject &sketch, const SketchConstraint &constraint);

struct PointTarget {
    ConstraintRef point;
    QPointF position;
};

struct SolveResult {
    bool ok = true;
    QString error;
    double residual = 0.0;
    int iterations = 0;
};

// Muove i punti dello schizzo finche' vincoli e bersagli sono soddisfatti.
// Se non ci riesce (vincoli in conflitto) lo schizzo resta com'era.
SolveResult solveSketch(SketchObject &sketch, const QVector<PointTarget> &targets = {});

// Gradi di liberta' dello schizzo: coordinate dei punti meno il rango delle
// equazioni dei vincoli (con quelle implicite delle curve e senza le liberta'
// che non cambiano la geometria, come il punto del raggio che gira sul
// cerchio). Un'entita' e' completamente definita se nessun movimento
// permesso dai vincoli sposta i suoi punti. Le maniglie delle spline non contano.
struct SketchAnalysis {
    int variables = 0, rank = 0, degreesOfFreedom = 0;
    QVector<bool> segmentDefined, curveDefined;
    bool fullyDefined() const { return degreesOfFreedom == 0; }
};
SketchAnalysis analyzeSketch(const SketchObject &sketch);

// Converte i vecchi dati dei vincoli (codici dei segmenti, lunghezze e angoli
// quotati, coincidenze) in vincoli oggetto, e li azzera.
void migrateLegacyConstraints(SketchObject &sketch);

// Rinumera i riferimenti dopo l'eliminazione di entita' (mappe vecchio ->
// nuovo indice, -1 eliminato; vuote = invariati): i vincoli che le usavano spariscono.
void remapConstraints(SketchObject &sketch, const QVector<int> &segmentMap, const QVector<int> &curveMap);

// Geometria delle quote per disegnarle: i due punti di una distanza (per
// punto-retta e retta-retta il secondo e' il piede sulla retta), le due rette
// di un angolo, centro e raggio di un cerchio o arco.
bool dimensionPoints(const SketchObject &sketch, const SketchConstraint &constraint, QPointF &p, QPointF &q);
bool constraintLines(const SketchObject &sketch, const SketchConstraint &constraint, QPointF &p0, QPointF &p1, QPointF &q0, QPointF &q1);
bool circleOf(const SketchObject &sketch, const ConstraintRef &ref, QPointF &center, double &radius);

// Posizione di un punto di riferimento (falso se non esiste).
bool refPoint(const SketchObject &sketch, const ConstraintRef &ref, QPointF &point);

}

#endif
