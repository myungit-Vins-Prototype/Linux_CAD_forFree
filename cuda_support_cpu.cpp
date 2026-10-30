#include "cuda_support.h"

const char *forgecad_cuda_backend() {
    return "CPU fallback (CUDA toolkit non disponibile)";
}

bool forgecad_cuda_available() {
    return false;
}

void *forgecad_cuda_upload_surface(const ForgeCudaBSplineSurface &) {
    return nullptr;
}

void forgecad_cuda_release_surface(void *) {}

bool forgecad_cuda_evaluate_surface(void *, const double *, std::size_t, double *, double *) {
    return false;
}

ForgeCudaPoolStats forgecad_cuda_pool_stats() { return {}; }
std::size_t forgecad_cuda_idle_bytes() { return 0; }
void forgecad_cuda_trim_idle() {}
