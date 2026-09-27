#ifndef FORGECAD_SKETCH_REFS_H
#define FORGECAD_SKETCH_REFS_H

#include <QString>
#include <QVector>

#include "cad_sketch_edit.h"
#include "cad_types.h"
#include "fk_curve.h"
#include "fk_topology.h"

// Riferimenti esterni dello schizzo: spigoli e curve dei corpi (anche eliche
// e spirali) e le sezioni dei solidi con il piano dello schizzo, portati nel
// piano come entita' esatte. La proiezione lungo la normale e' affine: rette
// -> segmenti, cerchi in piani paralleli -> cerchi o archi, il resto -> curva
// `Converted` (B-spline razionale con i poli proiettati e gli stessi pesi e
// nodi: la stessa curva proiettata; le eliche passano per la B-spline entro 1e-9).
namespace ForgeCad {

// Aggiunge allo schizzo la proiezione del tratto `range` di `curve`
// (coordinate del modello); `construction` le fa di costruzione, `fixed` le
// blocca con un vincolo fisso. Errore vuoto se riesce; `created` riceve le entita' nuove.
QString appendProjectedCurve(SketchObject &sketch, const Kernel::CurvePtr<3> &curve, const Kernel::Interval &range, bool construction, bool fixed,
                             QVector<SketchEntity> *created = nullptr);

// Spigolo del body piu' vicino a `point` (la sua curva e il suo tratto).
bool nearestBodyEdge(const Kernel::Body &body, const Kernel::Vec3 &point, Kernel::CurvePtr<3> &curve, Kernel::Interval &range);

// Le curve della sezione del solido con il piano dello schizzo (esatte: i
// bordi della parte del piano dentro il solido), aggiunte come sopra.
QString appendSectionCurves(SketchObject &sketch, const Kernel::Body &body, bool construction, bool fixed, QVector<SketchEntity> *created = nullptr);

}

#endif
