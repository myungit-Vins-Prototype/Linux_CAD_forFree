#ifndef FORGECAD_FK_PROFILE_H
#define FORGECAD_FK_PROFILE_H

#include <vector>

#include "fk_curve.h"

// Profili piani (gli schizzi): tratti di curva 2D concatenati in loop chiusi
// e raggruppati in regioni con fori, pronti per estrusione e simili.
namespace ForgeCad::Kernel {

// Tratto di curva: `range` e' la parte usata, percorsa da range.lo a range.hi.
struct ProfileSegment {
    CurvePtr<2> curve;
    Interval range;

    Vec2 start() const { return curve->point(range.lo); }
    Vec2 end() const { return curve->point(range.hi); }
};

// Loop chiuso: la fine di ogni tratto coincide (entro la tolleranza con cui
// e' stato costruito) con l'inizio del successivo. Un solo tratto chiuso
// (cerchio, spline chiusa) e' un loop valido.
struct ProfileLoop {
    std::vector<ProfileSegment> segments;
};

// Regione di materiale: contorno esterno antiorario e fori orari (la
// convenzione delle facce: il materiale sta a sinistra).
struct ProfileRegion {
    ProfileLoop outer;
    std::vector<ProfileLoop> holes;
};

struct Profile {
    std::vector<ProfileRegion> regions;
    int openChains = 0;  // catene di tratti che non si chiudono (escluse)
};

// Concatena i tratti per estremi coincidenti entro `tolerance` (ciascuno puo'
// essere percorso in un verso o nell'altro), poi annida i loop come fa lo
// schizzo di ForgeCAD: profondita' pari = materiale, dispari = foro della
// regione che lo contiene direttamente. Loop disgiunti e non intrecciati.
Profile buildProfile(const std::vector<ProfileSegment> &segments, double tolerance);

// Tratto e loop percorsi al contrario (stessa geometria).
ProfileSegment reversed(const ProfileSegment &segment);
ProfileLoop reversed(const ProfileLoop &loop);

// Area con segno (positiva se antioraria): 1/2 somma degli integrali di
// x y' - y x' sui tratti, con quadratura adattiva di Gauss-Kronrod.
double signedArea(const ProfileLoop &loop);
// Area di materiale di una regione (esterno meno fori).
double area(const ProfileRegion &region);

// Numero di avvolgimenti del loop attorno a p (integrale di d(theta) / 2 pi).
// Il punto non deve stare sul loop.
int windingNumber(const ProfileLoop &loop, const Vec2 &p);

}

#endif
