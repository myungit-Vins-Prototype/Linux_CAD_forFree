#include "gpu_tpms_support.h"
#include "kernel/fk_tpms_cells.h"

#include <cuda_runtime.h>
#include <cstdio>
#include <cstdlib>
#include <mutex>

namespace ForgeCad {
namespace {
using namespace Kernel;
constexpr unsigned blockSize = 256;
std::mutex workMutex;

struct CudaCellWork {
    CudaCellWork() = default;
    CudaCellWork(const CudaCellWork &) = delete;
    CudaCellWork &operator=(const CudaCellWork &) = delete;

    ~CudaCellWork() {
        if (output) cudaFree(output);
        if (field) cudaFree(field);
        if (stream) cudaStreamDestroy(stream);
    }

    bool allocate(std::size_t nodes, std::size_t cells) {
        return cudaStreamCreateWithFlags(&stream, cudaStreamNonBlocking) == cudaSuccess
            && cudaMalloc(reinterpret_cast<void **>(&field), nodes * sizeof(double)) == cudaSuccess
            && cudaMalloc(reinterpret_cast<void **>(&output), cells * sizeof(TpmsCell)) == cudaSuccess;
    }

    cudaStream_t stream = nullptr;
    double *field = nullptr;
    TpmsCell *output = nullptr;
};

__global__ void connectCells(const double *field, int strideY, int strideZ, int start, int width, int height,
                             int count, TpmsCell *out) {
    const int i = int(blockIdx.x * blockDim.x + threadIdx.x);
    if (i >= count) return;
    const int first = start + i % width + ((i / width) % height) * strideY + (i / (width * height)) * strideZ;
    out[i] = tpmsCellConnections(field, first, strideY, strideZ);
}

std::size_t gridCount(const TpmsGridIndex &size) {
    std::size_t count = 1;
    for (const auto dimension : size) {
        if (dimension <= 0 || std::size_t(dimension) > kMaxTpmsNodes / count) return 0;
        count *= std::size_t(dimension);
    }
    return count;
}

class CudaTpmsEvaluator final : public TpmsBatchEvaluator {
public:
    explicit CudaTpmsEvaluator(std::size_t minimumCells)
        : minimumCells_(minimumCells), profile_(std::getenv("FORGECAD_PROFILE") != nullptr) {}

    bool canBatch(std::size_t count) const override { return count > 0 && count >= minimumCells_; }

    bool cells(const std::vector<double> &field, const TpmsGridIndex &size, const TpmsGridIndex &first,
               const TpmsGridIndex &last, std::vector<TpmsCell> &connections) override {
        const auto nodes = gridCount(size);
        if (!nodes || nodes != field.size()) return false;
        TpmsGridIndex dimensions;
        for (int a = 0; a < 3; ++a) {
            if (first[a] < 0 || last[a] >= size[a] || first[a] >= last[a]) return false;
            dimensions[a] = last[a] - first[a];
        }
        const auto count = gridCount(dimensions);
        if (!canBatch(count)) return false;
        const std::unique_lock<std::mutex> lock(workMutex, std::try_to_lock);
        int devices = 0;
        if (!lock || cudaGetDeviceCount(&devices) != cudaSuccess || devices == 0) return false;
        // Un lotto alla volta, con memoria limitata dalle dimensioni della
        // griglia. Una richiesta concorrente passa subito alla CPU.
        const int strideZ = size[0] * size[1];
        const auto sliceNodes = std::size_t(strideZ) * (dimensions[2] + 1);
        std::vector<TpmsCell> next(count);
        CudaCellWork work;
        if (!work.allocate(sliceNodes, count)
            || cudaMemcpyAsync(work.field, field.data() + first[2] * strideZ, sliceNodes * sizeof(double),
                               cudaMemcpyHostToDevice, work.stream) != cudaSuccess) return false;
        const int start = first[0] + first[1] * size[0];
        connectCells<<<unsigned((count + blockSize - 1) / blockSize), blockSize, 0, work.stream>>>(
            work.field, size[0], strideZ, start, dimensions[0], dimensions[1], int(count), work.output);
        if (cudaGetLastError() != cudaSuccess
            || cudaMemcpyAsync(next.data(), work.output, count * sizeof(TpmsCell),
                               cudaMemcpyDeviceToHost, work.stream) != cudaSuccess
            || cudaStreamSynchronize(work.stream) != cudaSuccess) return false;
        connections = std::move(next);
        if (profile_) std::fprintf(stderr, "PROFILO TPMS CUDA: %zu celle\n", count);
        return true;
    }

private:
    const std::size_t minimumCells_;
    const bool profile_;
};

}

std::shared_ptr<Kernel::TpmsBatchEvaluator> makeCudaTpmsEvaluator(std::size_t minimumCells) {
    return std::make_shared<CudaTpmsEvaluator>(minimumCells);
}

}
