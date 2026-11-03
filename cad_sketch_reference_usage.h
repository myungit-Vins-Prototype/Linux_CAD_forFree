#ifndef FORGECAD_SKETCH_REFERENCE_USAGE_H
#define FORGECAD_SKETCH_REFERENCE_USAGE_H
#include "cad_types.h"
namespace ForgeCad {
struct SketchReferenceRemap {
    QVector<int> segments, curves; // vuote: invariati; altrimenti vecchio -> nuovo
    bool changed() const { return !segments.isEmpty() || !curves.isEmpty(); }
};
// Rimuove soltanto cache automatiche senza vincoli o consumatori. Conserva
// conversioni esplicite, ogni vincolo esistente e tutti i riferimenti a valle.
SketchReferenceRemap pruneUnusedSketchReferences(int index, QVector<SketchObject> &sketches,
    QVector<ExtrusionObject> &bodies, const QVector<ConstraintRef> &pending = {});
void pruneUnusedSketchReferences(DocumentState &state);
}
#endif
