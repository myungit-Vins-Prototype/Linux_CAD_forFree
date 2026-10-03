#ifndef FORGECAD_SKETCH_OFFSET_H
#define FORGECAD_SKETCH_OFFSET_H

#include <QVector>

#include "cad_sketch_edit.h"
#include "cad_types.h"

// Offset delle entita' dello schizzo: le entita' scelte si raccolgono in
// catene (estremi comuni) e ogni catena si copia a distanza costante.
//  - Segmenti -> segmenti paralleli (vincolo Parallelo con l'originale);
//    archi e cerchi -> archi e cerchi concentrici (vincolo Concentrico), con
//    il raggio R -/+ d: esatti.
//  - Spline, NURBS, ellissi e riferimenti convertiti -> B-spline cubiche C1
//    (curve "Converted") nello stesso parametro entro 1e-7 dalla curva a
//    distanza vera (come le parallele dei raccordi del kernel): la curva a
//    distanza di una spline non e' una spline.
//  - Negli angoli vivi della catena le copie si prolungano o si accorciano
//    fino al loro punto comune (come in SolidWorks); con `roundCorners` negli
//    angoli convessi c'e' un arco di raggio d attorno al vertice. Tra tratti
//    tangenti le copie sono gia' tangenti. Le copie consecutive hanno il
//    vincolo di coincidenza negli estremi.
//  - Verso: le catene chiuse vanno verso l'esterno, quelle aperte a
//    sinistra del primo tratto scelto; `reverse` gira il verso, `bothSides`
//    le copia dalle due parti.
// Errore (lo schizzo non cambia) se una copia degenera: raggio nullo o
// negativo, distanza oltre il raggio di curvatura (cuspidi), angolo concavo
// piu' profondo dei tratti.
namespace ForgeCad {

struct SketchOffset {
    double distance = 1.0;
    bool reverse = false;
    bool bothSides = false;
    bool roundCorners = false;
    bool construction = false;  // le entita' di partenza diventano di costruzione
};

SketchEditResult offsetSketchEntities(SketchObject &sketch, const QVector<SketchEntity> &entities, const SketchOffset &offset,
                                      QVector<SketchEntity> *created = nullptr);

}

#endif
