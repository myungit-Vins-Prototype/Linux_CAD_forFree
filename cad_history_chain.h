#ifndef FORGECAD_HISTORY_CHAIN_H
#define FORGECAD_HISTORY_CHAIN_H

#include "cad_types.h"
#include <QSet>

namespace ForgeCad {

// La base di una fusione/sottrazione di profilo e' lo stadio precedente del
// corpo logico, distinto dai riferimenti espliciti a facce, bordi e assi.
// Restituisce le feature ricollegate; un insieme vuoto considera tutti i corpi.
QVector<int> reconnectMergedHistory(QVector<ExtrusionObject> &features,
                                   const QSet<quint64> &bodyIds = {});

// Le cache dei consumatori di una definizione ricollegata non rappresentano
// piu' il documento. Propaga anche attraverso appoggi/riferimenti degli schizzi.
void invalidateHistoryCaches(DocumentState &state, const QVector<int> &changed);

}
#endif
