#ifndef FORGECAD_FK_HERMITE_H
#define FORGECAD_FK_HERMITE_H

#include <functional>
#include <vector>

#include "fk_bspline.h"

// Approssimazione di funzioni lisce t -> R^3 (piu' "righe" insieme, con gli
// stessi nodi) con B-spline di grado 5: su ogni tratto il polinomio di
// Hermite quintico che ha valore, derivata prima e seconda esatte negli
// estremi. Tra tratti della stessa parte liscia i nodi sono tripli (la curva
// e' C2, e i tre poli di ogni nodo dipendono solo dai dati in quel nodo); nei
// punti di rottura (`breaks`: dove le derivate della funzione saltano) i nodi
// hanno molteplicita' 5 e i dati si prendono da sinistra e da destra. I tratti
// si dimezzano finche' lo scarto dalla funzione vera, misurato in punti
// interni, e' sotto la tolleranza. Uso interno (elica, sweep).
namespace ForgeCad::Kernel::detail {

// out[3 r + k] = derivata k-esima (k = 0, 1, 2) della riga r in t; `left`: limite
// da sinistra (chiesto solo nei punti di rottura interni).
using RowSampler = std::function<void(double t, bool left, Vec3 *out)>;

struct RowSpline {
    std::vector<double> knots;               // espansi, grado 5
    std::vector<std::vector<Vec3>> poles;    // per riga
    double error = 0.0;                      // scarto massimo misurato
    std::vector<double> parameters;          // nodi distinti (estremi dei tratti)
};

// `breaks`: estremi e punti di rottura in ordine crescente; `maxStep`: lunghezza
// massima iniziale dei tratti (0: nessuna).
RowSpline fitQuinticRows(const RowSampler &sample, int rows, const std::vector<double> &breaks, double tolerance, double maxStep = 0.0);

// La riga r come curva.
BSplineCurve<3> rowCurve(const RowSpline &spline, int row);

}

#endif
