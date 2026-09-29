#ifndef FORGECAD_FEATURES_H
#define FORGECAD_FEATURES_H

#include <QString>
#include <vector>

#include "cad_types.h"
#include "fk_math.h"
#include "fk_sweep.h"

// Curve e percorsi: elica e spirale (fk_helix, esatta; la base da un corpo
// la legge forgeHelixBase di cad_forge) e percorsi degli sweep (catene di
// curve del kernel da uno schizzo o da un'elica).
namespace ForgeCad {

// Base di un'elica: punto dell'asse da cui parte, direzione dell'asse,
// riferimento dell'angolo 0 (perpendicolare all'asse), raggio iniziale.
// Da una faccia: la sua lunghezza lungo l'asse e, sul cono, la conicita'.
struct HelixBase {
    Kernel::Vec3 origin, axis, xRef;
    double radius = 0.0;
    double length = 0.0;     // 0: non nota
    bool hasTaper = false;   // conicita' imposta dalla faccia (cono)
    double taper = 0.0;      // radianti, positivo: il raggio cresce lungo l'asse
};

// Riferimento dell'angolo 0 per un asse: X del modello proiettato sul piano
// normale (Y se l'asse e' quasi parallelo a X).
Kernel::Vec3 helixReference(const Kernel::Vec3 &axis);

// Base da un cerchio o arco dello schizzo (curve = -1: il primo).
bool helixBaseFromSketch(const SketchObject &sketch, int curve, HelixBase &base, QString *error);

// Passo, giri e altezza effettivi (il valore che il modo non usa si ricava).
void helixDimensions(const HelixParameters &parameters, double &pitch, double &turns, double &height);
// Curva esatta (Kernel::HelixCurve) dai parametri e dalla base.
ForgeCurve helixCurve(const HelixParameters &parameters, const HelixBase &base, QString *error);
// Polilinea di visualizzazione della curva (quality 0/1/2).
void curveDisplay(const Kernel::Curve<3> &curve, int quality, BodyDisplay &display);

// Percorso da uno schizzo: le sue entita' (non di costruzione) devono formare
// una sola catena o un solo contorno chiuso.
bool sketchPath(const SketchObject &sketch, std::vector<Kernel::PathSegment> &path, QString *error);
// Componenti connesse della geometria non di costruzione e copia filtrata
// usata da sweep e loft per riferirsi a una parte dello schizzo.
QVector<SketchPathRef> sketchPathComponents(const SketchObject &sketch);
SketchObject sketchPathSubset(const SketchObject &sketch, const SketchPathRef &selection);
// Percorso da una curva (elica).
std::vector<Kernel::PathSegment> curvePath(const ForgeCurve &curve);
// Il percorso orientato perche' parta vicino al profilo: una catena si gira se
// il profilo sta alla sua fine, un contorno chiuso parte dal punto piu' vicino.
std::vector<Kernel::PathSegment> alignPath(const std::vector<Kernel::PathSegment> &path, const SketchObject &profile);

}

#endif
