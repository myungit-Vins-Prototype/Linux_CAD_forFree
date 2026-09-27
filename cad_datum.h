#ifndef FORGECAD_DATUM_H
#define FORGECAD_DATUM_H

#include <QString>
#include <QStringList>
#include <QVector>
#include <functional>

#include "cad_types.h"
#include "fk_math.h"

// Piani di costruzione (BodyFeature::DatumPlane): i riferimenti (GeometryRef)
// si risolvono sulla geometria esatta del corpo (body del kernel), degli
// schizzi e degli assi.
namespace ForgeCad {

// Ruoli dei riferimenti nei modi (maschera di bit).
enum DatumRole { DatumRolePlane = 1, DatumRolePoint = 2, DatumRoleLine = 4, DatumRoleCurve = 8 };

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

// Il piano del corpo `index` (origine = centro a video, asse X, normale).
bool computeDatum(const DatumParameters &parameters, int index, const QVector<SketchObject> &sketches, const QVector<ExtrusionObject> &bodies,
                  SketchFrame &frame, QString *error);

// Nome breve del riferimento (finestre, albero).
QString geometryRefText(const GeometryRef &ref, const QVector<SketchObject> &sketches, const QVector<ExtrusionObject> &bodies);

// Ruoli che un riferimento di questo tipo puo' avere (prima di risolverlo).
int geometryRefRoles(const GeometryRef &ref, const QVector<SketchObject> &sketches);

}

#endif
