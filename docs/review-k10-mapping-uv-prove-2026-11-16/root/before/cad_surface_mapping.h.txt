#pragma once
#include "cad_types.h"
namespace ForgeCad {
// targets[1].kind11 collega esplicitamente la mappa di un appiattimento.
// Risolve il piano originale e l'inversa completa, senza centratura geometrica.
bool resolveFlattenedMapping(SurfaceToolParameters &parameters, int owner,
    const QVector<ExtrusionObject> &bodies, GeometryRef &plane, QString *error = nullptr);
int sourceSurfaceFlattening(const QVector<GeometryRef> &sources, int owner,
    const QVector<ExtrusionObject> &bodies);
void buildSurfaceMapping(ExtrusionObject &feature, int owner, const QVector<SketchObject> &sketches,
                         const QVector<ExtrusionObject> &bodies);
}
