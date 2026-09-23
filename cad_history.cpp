#include "cad_history.h"

#include <utility>

namespace ForgeCad {

void History::record(const DocumentState &current) {
    undo_.append(current);
    if (undo_.size() > limit_) undo_.removeFirst();
    redo_.clear();
}

bool History::undo(DocumentState &current) {
    if (undo_.isEmpty()) return false;
    redo_.append(std::move(current));
    current = undo_.takeLast();
    return true;
}

bool History::redo(DocumentState &current) {
    if (redo_.isEmpty()) return false;
    undo_.append(std::move(current));
    current = redo_.takeLast();
    return true;
}

void History::clear() {
    undo_.clear();
    redo_.clear();
}

}
