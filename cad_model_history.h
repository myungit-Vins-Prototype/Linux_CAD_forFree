#ifndef FORGECAD_MODEL_HISTORY_H
#define FORGECAD_MODEL_HISTORY_H

#include "cad_types.h"

namespace ForgeCad {

// Completa/migra gli identificatori della storyboard e sincronizza il vecchio
// flag di visibilita' delle feature: per ogni corpo e' visibile soltanto il tip.
void normalizeModelHistory(DocumentState &state);
void normalizeModelHistory(QVector<ExtrusionObject> &features, QVector<ModelBody> &bodies);

// Corpo logico di cui la feature e' uno stadio per le sue dipendenze (la base
// di un raccordo, il primo corpo in cui si fonde un'estrusione, la prima
// superficie di una cucitura...), 0 se apre un corpo nuovo.
quint64 inheritedModelBody(const QVector<ExtrusionObject> &features, const ExtrusionObject &feature);

// Indice della feature finale e indici delle feature di un corpo, in ordine.
int modelBodyTipIndex(const DocumentState &state, quint64 bodyId);
QVector<int> modelBodyFeatures(const DocumentState &state, quint64 bodyId);

}

#endif
