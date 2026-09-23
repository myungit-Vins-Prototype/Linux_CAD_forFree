#ifndef FORGECAD_FK_MASS_H
#define FORGECAD_FK_MASS_H

#include "fk_topology.h"

// Proprieta' di massa calcolate sulla geometria esatta del B-rep (niente
// tassellazione). Con il teorema della divergenza gli integrali di volume
// diventano integrali di flusso sulle facce:
//   V = 1/3 \oint x.n dA,  \int x_i dV = 1/4 \oint x_i (x.n) dA,
//   \int x_i x_j dV = 1/5 \oint x_i x_j (x.n) dA,
// e ogni integrale su una faccia, nello spazio (u, v) della superficie,
// diventa con il teorema di Green un integrale lungo i loop:
//   \iint_D f du dv = \oint -G du,   G(u, v) = \int_{v0}^{v} f(u, s) ds,
// con G calcolata con una quadratura interna (come BRepGProp_Face di OCCT).
// Le curve dei loop in (u, v) sono le SP-curve esatte delle fin; dove
// mancano (o sono solo approssimate) si inverte la superficie sui punti
// degli edge.
//
// Facce periodiche senza cucitura: il termine in du non vede le cuciture
// in u (du = 0). I poli (sfera, vertice del cono, meridiano che tocca
// l'asse) e le facce del toro che contengono un giro completo si trattano
// aggiungendo le fasce complete di parametro che mancano al bordo.
// Non gestiti (std::domain_error): loop che si avvolgono nella direzione v
// di una superficie periodica in v (toro), facce aperte o illimitate.
namespace ForgeCad::Kernel {

struct MassProperties {
    double volume = 0.0;
    double area = 0.0;  // area della superficie di contorno
    Vec3 centroid;      // baricentro del volume (densita' unitaria)
    // Tensore d'inerzia rispetto al baricentro, assi globali, come
    // GProp_GProps::MatrixOfInertia: I_xx = \int (y^2 + z^2) dV, I_xy = -\int x y dV.
    double inertia[3][3] = {};
};

// Proprieta' di tutti i solidi del body (tutte le facce: le shell dei vuoti
// sono orientate verso il vuoto e sottraggono il loro volume).
// relativeTolerance: errore di quadratura relativo all'integrale di |f|.
MassProperties massProperties(const Body &body, double relativeTolerance = 1e-12);

// Area di una faccia.
double faceArea(const Body &body, FaceId face, double relativeTolerance = 1e-12);

}

#endif
