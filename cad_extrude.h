#ifndef FORGECAD_EXTRUDE_H
#define FORGECAD_EXTRUDE_H

#include <QString>
#include <QVector>
#include <functional>

#include "cad_types.h"
#include "fk_topology.h"

// Estrusione con le condizioni di fine (fino a un punto, a uno spigolo, a una
// faccia o a un piano) e la fusione con altri solidi (unione o sottrazione,
// anche con i corpi scelti in automatico). Tutto sul B-rep esatto.
namespace ForgeCad {

// Corpo dell'estrusione `body` (BodyFeature::Extrusion, indice `index`): i
// riferimenti e i corpi da fondere vengono da `sketches` e `bodies` (indici
// minori). Con `body.mergeProbe` i corpi di mergeBodies sono candidati e vi
// restano solo quelli che hanno punti in comune con l'estrusione.
ForgeBody forgeExtrusionFeature(ExtrusionObject &body, int index, const QVector<SketchObject> &sketches, const QVector<ExtrusionObject> &bodies,
                                QString *error);

// Estrusione e corpo hanno punti in comune: per l'unione si toccano (il
// risultato ha meno componenti), per la sottrazione si sovrappongono.
bool forgeSharesMaterial(const ForgeBody &tool, const ForgeBody &body, bool subtract);

// Solo le shell di `body` per cui `keep` e' vero, come body nuovo (vuoto: nessuna).
ForgeBody forgeKeepShells(const ForgeBody &body, const std::function<bool(const Kernel::Body &, Kernel::ShellId)> &keep);

}

#endif
