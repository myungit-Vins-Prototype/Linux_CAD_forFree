#ifndef FORGECAD_HISTORY_H
#define FORGECAD_HISTORY_H

#include "cad_types.h"

namespace ForgeCad {

// Cronologia Undo/Redo basata su istantanee complete del documento.
class History {
public:
    explicit History(int limit = 200) : limit_(limit) {}

    // Da chiamare prima di modificare il documento, con lo stato corrente.
    void record(const DocumentState &current);
    bool undo(DocumentState &current);
    bool redo(DocumentState &current);
    void clear();

    bool canUndo() const { return !undo_.isEmpty(); }
    bool canRedo() const { return !redo_.isEmpty(); }

private:
    QVector<DocumentState> undo_;
    QVector<DocumentState> redo_;
    int limit_;
};

}

#endif
