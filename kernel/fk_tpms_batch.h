#ifndef FORGECAD_FK_TPMS_BATCH_H
#define FORGECAD_FK_TPMS_BATCH_H

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace ForgeCad::Kernel {

inline constexpr std::size_t kMaxTpmsNodes = 4500000;

using TpmsGridIndex = std::array<int, 3>;
using TpmsCell = std::uint64_t;

// Connettività locale delle celle, calcolata dai valori double del campo.
// Il kernel resta responsabile di vertici, cucitura e geometria B-rep.
// Al fallimento l'uscita resta invariata e il chiamante usa la CPU.
class TpmsBatchEvaluator {
public:
    virtual ~TpmsBatchEvaluator() = default;

    virtual bool canBatch(std::size_t count) const = 0;
    virtual bool cells(const std::vector<double> &field, const TpmsGridIndex &size,
                       const TpmsGridIndex &first, const TpmsGridIndex &last,
                       std::vector<TpmsCell> &connections) = 0;
};

inline std::shared_ptr<TpmsBatchEvaluator> tpmsBatchEvaluator;
inline void setTpmsBatchEvaluator(std::shared_ptr<TpmsBatchEvaluator> evaluator) {
    std::atomic_store(&tpmsBatchEvaluator, std::move(evaluator));
}
inline std::shared_ptr<TpmsBatchEvaluator> getTpmsBatchEvaluator() {
    return std::atomic_load(&tpmsBatchEvaluator);
}

}

#endif
