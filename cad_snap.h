#ifndef FORGECAD_SNAP_H
#define FORGECAD_SNAP_H

#include "cad_types.h"

namespace ForgeCad {

// Un solo aggancio per punto; le intersezioni restano una scelta separata.
struct PointAttachment {
    ConstraintRef target;
    ConstraintType type = ConstraintType::Coincident;
    double value = 0.0;
    int priority = 4; // punto, quadrante, punto medio, curva
    bool external = false; // indici nella cache transitoria
};
PointAttachment pointAttachment(const QPointF &point, const SketchObject &local,
                                const SketchObject &external, double tolerance, bool origin,
                                const QVector<ConstraintRef> &excluded = {});

// Materializza soltanto l'entita esatta scelta nella cache; indici rimappati dopo append.
ConstraintRef resolvePointAttachment(SketchObject &sketch, const PointAttachment &attachment,
                                     const SketchObject &external, const QVector<SketchObject> &sketches,
                                     const QVector<ExtrusionObject> &bodies);

struct SnapResult {
    SnapKind kind = SnapKind::None;
    QPointF point;
};

// Coppia di entita' esatte: gli indici esterni appartengono alla cache transitoria.
// Nessun riferimento persistente viene creato durante il movimento del cursore.
struct IntersectionSnap {
    bool found = false;
    QPointF point;
    ConstraintRef first, second;
    bool firstExternal = false, secondExternal = false;
};
IntersectionSnap snapIntersection(const QPointF &point, const SketchObject &local,
                                  const SketchObject &external, double tolerance,
                                  const QVector<ConstraintRef> &excluded = {});
// Conserva solo le due sorgenti usate e due PointOnCurve nel normale Undo del
// chiamante. Operazione atomica; non introduce nuovi tipi di vincolo nel codec.
bool constrainIntersection(SketchObject &sketch, const ConstraintRef &point,
                           const IntersectionSnap &snap, const SketchObject &external,
                           const QVector<SketchObject> &sketches, const QVector<ExtrusionObject> &bodies);

// Aggancio a estremi, punti medi e punto piu' vicino dei segmenti e ai punti
// notevoli `points` (estremi delle curve, centri, vertici, origine); i punti
// entro la tolleranza vincono sul punto piu' vicino dei segmenti; altrimenti griglia.
// I due agganci sono indipendenti: `snapToGeometry` (punti e segmenti) e `snapToGrid`.
SnapResult snapSegments(const QPointF &point, const QVector<SketchSegment> &segments,
                        const QVector<QPointF> &points, bool snapToGeometry, bool snapToGrid,
                        double gridSpacing, double tolerance);

}

#endif
