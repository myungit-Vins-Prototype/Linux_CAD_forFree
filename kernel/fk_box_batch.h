#ifndef FORGECAD_FK_BOX_BATCH_H
#define FORGECAD_FK_BOX_BATCH_H

#include "fk_intersect.h"
#include <atomic>
#include <memory>
#include <vector>

namespace ForgeCad::Kernel {

// Solo filtro conservativo: false nella maschera certifica box disgiunti;
// true richiede ancora il controllo double e l'intersezione del kernel.
// Un errore non consegna risultati parziali: il chiamante usa il BVH CPU.
class BoxBatchEvaluator {
public:
    virtual ~BoxBatchEvaluator() = default;
    virtual bool overlaps(const std::vector<Box> &first, const std::vector<Box> &second,
                          std::vector<unsigned char> &mask) = 0;
};

inline std::shared_ptr<BoxBatchEvaluator> boxBatchEvaluator;
inline void setBoxBatchEvaluator(std::shared_ptr<BoxBatchEvaluator> evaluator) {
    std::atomic_store(&boxBatchEvaluator,std::move(evaluator));
}
inline std::shared_ptr<BoxBatchEvaluator> getBoxBatchEvaluator() {
    return std::atomic_load(&boxBatchEvaluator);
}

}
#endif
