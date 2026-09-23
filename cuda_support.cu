#include "cuda_support.h"

#include <cuda_runtime.h>

__global__ void forgecad_cuda_probe_kernel() {}

const char *forgecad_cuda_backend() {
    return "CUDA backend (runtime NVIDIA)";
}

bool forgecad_cuda_available() {
    int deviceCount = 0;
    if (cudaGetDeviceCount(&deviceCount) != cudaSuccess || deviceCount == 0) {
        return false;
    }
    forgecad_cuda_probe_kernel<<<1, 1>>>();
    return cudaDeviceSynchronize() == cudaSuccess;
}
