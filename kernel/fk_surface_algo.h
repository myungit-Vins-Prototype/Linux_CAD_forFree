#ifndef FORGECAD_FK_SURFACE_ALGO_H
#define FORGECAD_FK_SURFACE_ALGO_H

#include "fk_surface.h"

// Interrogazioni sulle superfici.
namespace ForgeCad::Kernel {

struct SurfaceProjection {
    double u = 0.0;
    double v = 0.0;
    Vec3 point;
    double distance = 0.0;
};

// Punto della pezza [uRange] x [vRange] piu' vicino a p, bordi compresi.
// Il minimo e' o un punto stazionario interno o sul bordo: i bordi si
// trattano con la proiezione sulle isoparametriche (esatta su rette, cerchi
// e B-spline), l'interno cosi':
//  - piano e cilindro: forma chiusa completa (anche con intervalli infiniti);
//  - cono, sfera, toro: punti stazionari in forma chiusa + bordi;
//  - B-spline: branch and bound sulle pezze di Bezier (limite inferiore dal
//    box dei poli) + Newton: nessun minimo perso;
//  - estrusione: riduzione esatta alla curva base proiettata (anche con v
//    infinito);
//  - rivoluzione con meridiano piano: riduzione esatta al meridiano;
//  - altri casi (meridiano sghembo): griglia di campioni e Newton, con
//    intervalli finiti; qui un minimo molto stretto puo' ancora sfuggire.
SurfaceProjection projectPoint(const Surface &surface, const Vec3 &p, const Interval &uRange, const Interval &vRange);
SurfaceProjection projectPoint(const Surface &surface, const Vec3 &p);

// Parametri di un punto che sta sulla superficie (entro `tolerance`), vicino
// a `uv` (in ingresso la stima, in uscita il risultato): Gauss-Newton dalla
// stima, e se non converge la proiezione globale. I parametri periodici
// vengono portati entro mezzo periodo dalla stima, cosi' una successione di
// punti vicini da' parametri continui. `scale`: dimensione del modello, per
// il criterio di arresto. Falso se il punto dista piu' di `tolerance`.
bool invertPoint(const Surface &surface, const Vec3 &p, Vec2 &uv, double tolerance, double scale);

}

#endif
