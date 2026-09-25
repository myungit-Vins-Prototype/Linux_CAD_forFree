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

// Le due superfici sono lo stesso insieme di punti (entro `tolerance`), con
// parametrizzazioni anche diverse: piani coincidenti, cilindri coassiali
// dello stesso raggio, sfere, coni e tori uguali, superfici estruse in
// direzioni parallele dalla stessa curva (la curva base di ciascuna sta
// sull'altra). Per le altre superfici solo se sono lo stesso oggetto.
bool sameSurface(const Surface &a, const Surface &b, double tolerance);

// Poli: punti in cui la parametrizzazione degenera (Su nulla per ogni u) a
// una quota v: poli della sfera, vertice del cono, estremi del meridiano di
// una rivoluzione che stanno sull'asse. Una curva che ci passa ha li' un
// vertice; nello spazio (u, v) i loop che lo toccano "camminano" lungo la
// linea v = quota del polo (vedi poleWalk).
struct SurfacePole {
    Vec3 point;
    double v = 0.0;
};
std::vector<SurfacePole> surfacePoles(const Surface &surface);
// Indice del polo entro `tolerance` da p, o -1.
int poleIndex(const std::vector<SurfacePole> &poles, const Vec3 &p, double tolerance);

// Cammino lungo la linea del polo tra un loop che arriva con u = `from` e
// riparte con u congruo a `to` (modulo il periodo): il dominio della faccia
// sta a sinistra (verso della faccia `sense`) e il polo sta sopra (`top`: i
// punti vicini hanno v minore) o sotto. Restituisce il valore di arrivo,
// congruo a `to`, a meno di un periodo nel verso giusto da `from`.
double poleWalk(double from, double to, double period, bool top, bool sense);
// Lo stesso, con il giro deciso dai valori di u di due punti dei tratti poco
// lontani dal polo (`fromNear`, `toNear`, srotolati come `from` e `to`): due
// tratti tangenti nel polo (una cuspide) hanno lo stesso u limite, e solo
// l'ordine vicino al polo dice da che parte sta il dominio.
double poleWalk(double from, double to, double period, bool top, bool sense, double fromNear, double toNear);

// Normale della superficie anche nei poli: nei punti regolari quella di
// Su x Sv; nel polo la media delle normali su un anello di quote vicine
// dalla parte `side` (+1: v maggiori; 0: la parte interna al dominio). Nel
// vertice del cono e' l'asse, il verso in cui la falda si proietta sul piano
// perpendicolare conservando l'orientamento.
Vec3 normalAt(const Surface &surface, double u, double v, int side = 0);

}

#endif
