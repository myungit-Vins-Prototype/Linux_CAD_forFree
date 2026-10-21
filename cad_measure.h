#ifndef FORGECAD_MEASURE_H
#define FORGECAD_MEASURE_H

#include <QString>
#include <QStringList>
#include <QVector>
#include <memory>
#include <vector>

#include "cad_types.h"
#include "fk_math.h"
#include "fk_sweep.h"
#include "fk_topology.h"

// Misure nella vista (*Analisi → Misura*): punti, spigoli e curve, facce,
// piani e assi scelti con gli agganci della scelta dei riferimenti
// (GeometryRef), misurati sulla geometria esatta del kernel. La distanza tra
// due entita' e' la minima: proiezioni esatte alternate da semi presi sulla
// geometria esatta (mai dalla tassellazione); rette, piani e punti in forma
// chiusa.
namespace ForgeCad {

// Tipi di GeometryRef usati solo dalla misura (non si salvano nei documenti):
// punto medio di uno spigolo e centro di uno spigolo circolare o ellittico del
// corpo `index`, lo spigolo dato da `point` come nei riferimenti di spigolo.
inline constexpr int kGeometryRefEdgeMidpoint = 11;
inline constexpr int kGeometryRefEdgeCenter = 12;
// Bit in piu' dei ruoli di CadViewport::beginReferencePick (DatumRole):
// aggancio anche ai punti medi e ai centri degli spigoli.
inline constexpr int kMeasureSnapRole = 1 << 8;

struct MeasureEntity {
    enum class Kind { None, Point, Line, Plane, Curve, Face };
    Kind kind = Kind::None;
    QString name;
    Kernel::Vec3 point;      // Point; Line e Plane: un loro punto
    Kernel::Vec3 direction;  // Line: direzione unitaria; Plane: normale unitaria
    std::vector<Kernel::PathSegment> segments;  // Curve
    ForgeBody body;                             // Face
    Kernel::FaceId face;
};

// Il riferimento come entita' esatta; falso con l'errore.
bool measureEntity(const GeometryRef &ref, const QVector<SketchObject> &sketches, const QVector<ExtrusionObject> &bodies,
                   MeasureEntity &entity, QString *error);

struct MeasureReport {
    bool ok = false;
    QString error;
    QStringList lines;  // righe "Nome: valore" (testo semplice)
    // Distanza minima: i due punti piu' vicini (anche per disegnarla).
    bool hasDistance = false;
    Kernel::Vec3 from, to;
    double distance = 0.0;
    QString label;  // breve, per la vista
};

// Una entita': le sue misure (coordinate, lunghezza, raggio, area...); due:
// distanza minima, componenti, angolo, distanza tra i centri.
MeasureReport measureEntities(const QVector<MeasureEntity> &entities);

// Punto medio (a meta' lunghezza) e centro di uno spigolo (falso se non e'
// un cerchio, un arco o un'ellisse).
Kernel::Vec3 edgeMidpoint(const Kernel::Body &body, Kernel::EdgeId edge);
bool edgeCenter(const Kernel::Body &body, Kernel::EdgeId edge, Kernel::Vec3 &center);

}

#endif
