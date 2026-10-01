#ifndef FORGECAD_MODEL_HISTORY_H
#define FORGECAD_MODEL_HISTORY_H

#include "cad_types.h"

namespace ForgeCad {

// Completa/migra gli identificatori della storyboard e sincronizza il vecchio
// flag di visibilita' delle feature: per ogni corpo e' visibile soltanto il tip.
void normalizeModelHistory(DocumentState &state);
void normalizeModelHistory(QVector<ExtrusionObject> &features, QVector<ModelBody> &bodies);

// Indice della feature finale e indici delle feature di un corpo, in ordine.
int modelBodyTipIndex(const DocumentState &state, quint64 bodyId);
QVector<int> modelBodyFeatures(const DocumentState &state, quint64 bodyId);

}

#endif
