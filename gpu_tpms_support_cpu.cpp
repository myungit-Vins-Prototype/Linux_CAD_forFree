#include "gpu_tpms_support.h"

namespace ForgeCad {

std::shared_ptr<Kernel::TpmsBatchEvaluator> makeCudaTpmsEvaluator(std::size_t) { return {}; }

}
