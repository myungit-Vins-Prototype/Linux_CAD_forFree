#ifndef FORGECAD_FK_SHEET_GYROID_H
#define FORGECAD_FK_SHEET_GYROID_H

#include "fk_quantize.h"

#include <memory>

namespace ForgeCad::Kernel {

// Passo massimo per interpolare le due facce della parete.
double curvedGyroidSamplingLimit(const SheetGyroid &pattern);
void validateCurvedGyroid(const Box &bounds, double pitch, const SheetGyroid &pattern);

enum class GyroidWall { Faceted, Spline };

// Campionamento immutabile del solo inviluppo, riusabile al cambio dei
// parametri della parete. Non conserva il corpo sorgente.
class CurvedGyroidEnvelope;
std::shared_ptr<const CurvedGyroidEnvelope> sampleCurvedGyroidEnvelope(const Body &source, double pitch);
Body curvedGyroidSolid(const Body &source, const CurvedGyroidEnvelope &envelope, const SheetGyroid &pattern,
                       int axis, double from, double to, GyroidWall wall = GyroidWall::Faceted);

// Interpola il contorno del materiale in doppia precisione e ne costruisce
// il B-rep, con la parete interpolata a spline in modalità sperimentale.
// Il B-rep sorgente fornisce l'inviluppo per intersezione
// con le rette della griglia. Fuori dall'intervallo il campo resta pieno.
Body curvedGyroidSolid(const Body &source, double pitch, const SheetGyroid &pattern,
                       int axis = -1, double from = 0, double to = 0, GyroidWall wall = GyroidWall::Faceted);

// Raffina gli attraversamenti, costruisce i taglienti a spline e li
// sottrae al B-rep originale. L'intervallo limita il materiale asportato.
Body trimmedSplineGyroidSolid(const Body &source, double pitch, const SheetGyroid &pattern,
                             int axis = -1, double from = 0, double to = 0);

}

#endif
