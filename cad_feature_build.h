#ifndef FORGECAD_FEATURE_BUILD_H
#define FORGECAD_FEATURE_BUILD_H

#include "cad_types.h"

namespace ForgeCad {

// Costruzione della feature da copie di schizzi e stadi precedenti. Il modulo
// non accede alla GUI e puo' essere usato dai worker e dalle anteprime.
// buildGeometry aggiorna risultato, tipo ed errore; buildBody gestisce anche
// cache al caricamento, tessellazione e istanze delle ripetizioni.
void buildGeometry(ExtrusionObject &body, int index, const QVector<SketchObject> &sketches,
                   const QVector<ExtrusionObject> &bodies);
void buildBody(ExtrusionObject &body, int index, const QVector<SketchObject> &sketches,
               const QVector<ExtrusionObject> &bodies, int quality, bool prepareDisplay = true);
void tessellateGeometry(const ExtrusionObject &body, int quality, BodyDisplay &display);
void configurePatternInstances(ExtrusionObject &body, int index, const QVector<SketchObject> &sketches,
                               const QVector<ExtrusionObject> &bodies);

}

#endif
