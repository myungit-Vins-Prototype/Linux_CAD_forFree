#include "cuda_support.h"

const char *forgecad_cuda_backend() {
    return "CPU fallback (CUDA toolkit non disponibile)";
}

bool forgecad_cuda_available() {
    return false;
}
