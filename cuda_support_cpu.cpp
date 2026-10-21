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
