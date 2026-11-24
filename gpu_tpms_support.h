#ifndef FORGECAD_GPU_TPMS_SUPPORT_H
#define FORGECAD_GPU_TPMS_SUPPORT_H

#include "kernel/fk_tpms_batch.h"

namespace ForgeCad {

std::shared_ptr<Kernel::TpmsBatchEvaluator> makeCudaTpmsEvaluator(std::size_t minimumCells);

}
#endif
