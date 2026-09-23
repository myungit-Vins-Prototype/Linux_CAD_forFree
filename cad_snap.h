#ifndef FORGECAD_SNAP_H
#define FORGECAD_SNAP_H

#include "cad_types.h"

namespace ForgeCad {

struct SnapResult {
    SnapKind kind = SnapKind::None;
    QPointF point;
};

// Aggancio a estremi, punti medi e punto piu' vicino dei segmenti e ai punti
// notevoli `points` (estremi delle curve, centri, vertici); altrimenti griglia.
SnapResult snapSegments(const QPointF &point, const QVector<SketchSegment> &segments,
                        const QVector<QPointF> &points, bool enabled, bool snapToGrid,
                        double gridSpacing, double tolerance);

}

#endif
