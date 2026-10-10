#ifndef FORGECAD_DATUM_H
#define FORGECAD_DATUM_H

#include <QString>
#include <QStringList>
#include <QVector>
#include <functional>

#include "cad_types.h"
#include "fk_math.h"
#include "fk_sweep.h"

// Piani di costruzione (BodyFeature::DatumPlane): i riferimenti (GeometryRef)
// si risolvono sulla geometria esatta del corpo (body del kernel), degli
// schizzi e degli assi.
namespace ForgeCad {

// Ruoli dei riferimenti nei modi (maschera di bit).
// DatumRoleFace: una faccia qualsiasi, anche curva (la fine di un'estrusione).
enum DatumRole { DatumRolePlane = 1, DatumRolePoint = 2, DatumRoleLine = 4, DatumRoleCurve = 8, DatumRoleFace = 16 };

struct DatumMode {
    QString name;
    QVector<int> roles;   // un ruolo per riferimento
    QStringList labels;   // etichetta di ogni riferimento nella finestra
    bool distance = false, angle = false, onCurve = false;
};
// I modi di DatumParameters::mode, nell'ordine.
const QVector<DatumMode> &datumModes();

// Riferimento risolto: cosa puo' fare (punto, retta, piano, curva) e la sua geometria.
struct ResolvedRef {
    bool hasPoint = false, hasLine = false, hasPlane = false, hasCurve = false;
    Kernel::Vec3 point;      // il punto; per retta e piano un loro punto
    Kernel::Vec3 direction;  // retta: direzione unitaria; piano: normale unitaria
    // Curva: punto piu' vicino a q e tangente unitaria in quel punto.
    std::function<bool(const Kernel::Vec3 &q, Kernel::Vec3 &foot, Kernel::Vec3 &tangent)> nearest;
};

// Risolve il riferimento per il corpo `owner` (i corpi usati devono avere indice minore).
bool resolveGeometryRef(const GeometryRef &ref, int owner, const QVector<SketchObject> &sketches, const QVector<ExtrusionObject> &bodies,
                        ResolvedRef &resolved, QString *error);

// Tratti esatti di un riferimento curva, usato per intero: lo spigolo del
// corpo (la sua curva sul suo intervallo), l'entita' dello schizzo (il
// segmento, o i tratti di curveGeometry portati nel piano dello schizzo), la
// curva di un corpo curva (elica, spirale) sul suo dominio. Gli assi del
// modello e gli altri riferimenti non sono curve limitate: false con l'errore.
// Curva di un corpo curva (eliche, curve proiettate) per un riferimento:
// con piu' curve (ProjectedCurve) quella piu' vicina al punto del riferimento.
ForgeCurve bodyCurveNear(const ExtrusionObject &body, const EdgePoint &point);

bool geometryRefPath(const GeometryRef &ref, int owner, const QVector<SketchObject> &sketches, const QVector<ExtrusionObject> &bodies,
                     std::vector<Kernel::PathSegment> &segments, QString *error);

// Il piano del corpo `index` (origine = centro a video, asse X, normale).
bool computeDatum(const DatumParameters &parameters, int index, const QVector<SketchObject> &sketches, const QVector<ExtrusionObject> &bodies,
                  SketchFrame &frame, QString *error);

// Nome breve del riferimento (finestre, albero).
QString geometryRefText(const GeometryRef &ref, const QVector<SketchObject> &sketches, const QVector<ExtrusionObject> &bodies);

// Ruoli che un riferimento di questo tipo puo' avere (prima di risolverlo).
int geometryRefRoles(const GeometryRef &ref, const QVector<SketchObject> &sketches);

}

#endif
