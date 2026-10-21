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
    const auto identity = current.version; const auto base = current.baseCheckinId;
    redo_.append(std::move(current));
    current = undo_.takeLast();
    current.version = identity; current.baseCheckinId = base;
    return true;
}

bool History::redo(DocumentState &current) {
    if (redo_.isEmpty()) return false;
    const auto identity = current.version; const auto base = current.baseCheckinId;
    undo_.append(std::move(current));
    current = redo_.takeLast();
    current.version = identity; current.baseCheckinId = base;
    return true;
}

void History::clear() {
    undo_.clear();
    redo_.clear();
}

}
