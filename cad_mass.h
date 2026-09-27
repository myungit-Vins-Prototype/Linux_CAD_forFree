#ifndef FORGECAD_MASS_H
#define FORGECAD_MASS_H

#include <QString>
#include <QVector>

#include "cad_types.h"

// Proprieta' di massa dei corpi (finestra "Proprieta' di massa"), sulla
// geometria esatta del kernel proprio (fk_mass, integrali sulle superfici
// esatte a 1e-12). Unita' del modello
// (mm); densita' unitaria: la massa e i momenti si ottengono moltiplicando per
// la densita'.
namespace ForgeCad {

struct MassReport {
    enum class Kind { Solid, Sheet, Curve };
    Kind kind = Kind::Solid;
    bool ok = false;
    QString error;
    QString method;       // come e' stato calcolato
    double volume = 0.0;  // solidi
    double area = 0.0;    // area della superficie di contorno (solidi) o della lamina
    double length = 0.0;  // curve
    // Solidi: baricentro del volume e tensore d'inerzia rispetto al baricentro
    // (assi del modello, densita' 1): I_xx = \int (y^2 + z^2) dV, I_xy = -\int x y dV.
    double centroid[3] = {0.0, 0.0, 0.0};
    double inertia[3][3] = {};
    bool hasCentroid = false;
};

// Proprieta' del body del kernel proprio.
MassReport forgeMassProperties(const Kernel::Body &body);
// Lunghezza di una curva (elica), con il baricentro della curva.
MassReport curveMassProperties(const Kernel::Curve<3> &curve);

// Somma di piu' solidi (volumi che non si sovrappongono): baricentro comune e
// tensore rispetto a esso (teorema di Huygens-Steiner).
MassReport combineMass(const QVector<MassReport> &parts);

// Tensore rispetto al punto `point` (Huygens-Steiner, densita' 1).
void inertiaAbout(const MassReport &report, const double point[3], double result[3][3]);

// Momenti principali (autovalori crescenti) e assi principali (colonne
// axes[.][k], terna destrorsa) di un tensore simmetrico (Jacobi).
void principalMoments(const double tensor[3][3], double moments[3], double axes[3][3]);

}

#endif
