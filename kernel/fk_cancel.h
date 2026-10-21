#ifndef FORGECAD_FK_CANCEL_H
#define FORGECAD_FK_CANCEL_H

#include <atomic>
#include <memory>
#include <utility>

namespace ForgeCad::Kernel {

// Distinct from geometry errors: cancellation must bypass their recovery paths.
struct OperationCancelled {};
using CancellationToken = std::shared_ptr<std::atomic<bool>>;
inline thread_local CancellationToken currentCancellation;

class CancellationScope {
    CancellationToken previous_;
public:
    explicit CancellationScope(CancellationToken token)
        : previous_(std::move(currentCancellation)) { currentCancellation = std::move(token); }
    ~CancellationScope() { currentCancellation = std::move(previous_); }
    CancellationScope(const CancellationScope &) = delete;
    CancellationScope &operator=(const CancellationScope &) = delete;
};

inline void cancellationPoint() {
    if (currentCancellation && currentCancellation->load(std::memory_order_relaxed))
        throw OperationCancelled{};
}

}
#endif
