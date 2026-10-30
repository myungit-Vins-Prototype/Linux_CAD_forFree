#ifndef FORGECAD_GPU_BROAD_PHASE_H
#define FORGECAD_GPU_BROAD_PHASE_H
#include "fk_box_batch.h"
#include <cstdint>

namespace ForgeCad {
// Percorso sperimentale condiviso macOS/Metal e Linux/CUDA. Abilitazione
// esplicita FORGECAD_GPU_BROAD_PHASE=1; il default resta il BVH CPU.
void initializeGeometryAcceleration();
std::shared_ptr<Kernel::BoxBatchEvaluator> makeGpuBoxEvaluator();
std::uint64_t gpuBoxPairsProcessed();
}
#endif
