#ifndef FORGECAD_FK_SURFACE_DISTANCE_H
#define FORGECAD_FK_SURFACE_DISTANCE_H

#include "fk_surface.h"

namespace ForgeCad::Kernel {

// Zero se la distanza minima e' entro tolerance; altrimenti la distanza
// del proiettore globale originale (non la differenza dalla tolleranza).
// Un punto verificato della superficie puo' certificare il caso entro
// soglia senza calcolare il minimo globale. I casi incerti usano projectPoint.
// Sotto la risoluzione lineare si usa il proiettore globale precedente.
// tolerance deve essere finita e non negativa. hint e' soltanto una stima
// in/out per punti consecutivi, locale al chiamante; puo' essere ignorata.
double surfaceDeviationAboveTolerance(const Surface &surface, const Vec3 &p, double tolerance, Vec2 *hint = nullptr);

}

#endif
