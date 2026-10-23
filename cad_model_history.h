#ifndef FORGECAD_MODEL_HISTORY_H
#define FORGECAD_MODEL_HISTORY_H

#include "cad_types.h"
#include <functional>

namespace ForgeCad {

// Completa/migra gli identificatori della storyboard e sincronizza il vecchio
// flag di visibilita' delle feature: per ogni corpo e' visibile soltanto
// l'ultimo stadio valido; le feature successive in errore restano nella storia.
void normalizeModelHistory(DocumentState &state);
void normalizeModelHistory(QVector<ExtrusionObject> &features, QVector<ModelBody> &bodies);

// Sequenza unificata: gli schizzi conservano una posizione propria anche
// quando nessuna lavorazione li usa. Gli indici sono solo viste operative.
QVector<StoryStep> modelStory(const QVector<SketchObject> &sketches, const QVector<ExtrusionObject> &features);
void normalizeSketchHistory(QVector<SketchObject> &sketches, const QVector<ExtrusionObject> &features);
void placeStorySketches(QVector<SketchObject> &sketches, const QVector<ExtrusionObject> &features, const QVector<StoryStep> &order);
QString storySketchDependencyError(const QVector<SketchObject> &sketches, const QVector<ExtrusionObject> &features,
                                   const QVector<StoryStep> &order);

// Ricalcola le cache operative degli appoggi dopo riordino/eliminazione,
// mantenendo l'identita' persistente e senza mutare le istantanee Undo.
void remapSketchSupports(QVector<SketchObject> &sketches, const std::function<int(int)> &map);

// Corpo logico di cui la feature e' uno stadio per le sue dipendenze (la base
// di un raccordo, il primo corpo in cui si fonde un'estrusione, la prima
// superficie di una cucitura...), 0 se apre un corpo nuovo.
quint64 inheritedModelBody(const QVector<ExtrusionObject> &features, const ExtrusionObject &feature);

// Indice dell'ultimo stadio valido e indici delle feature di un corpo, in ordine.
int modelBodyTipIndex(const DocumentState &state, quint64 bodyId);
QVector<int> modelBodyFeatures(const DocumentState &state, quint64 bodyId);

}

#endif
