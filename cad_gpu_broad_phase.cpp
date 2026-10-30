#include "cad_gpu_broad_phase.h"
#include "gpu_box_support.h"

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <limits>

namespace ForgeCad {
namespace {
std::atomic<std::uint64_t> gpuPairs{0};
class GpuBoxEvaluator final : public Kernel::BoxBatchEvaluator {
public:
    bool overlaps(const std::vector<Kernel::Box> &a,const std::vector<Kernel::Box> &b,
                  std::vector<unsigned char> &mask) override {
        constexpr std::size_t budget=16*1024*1024;
        if(a.empty()||b.empty()||a.size()>budget/b.size())return false;
        std::vector<float> first,second;
        if(!pack(a,first)||!pack(b,second))return false;
        std::vector<unsigned char> next(a.size()*b.size());
        if(!forgecad_gpu_box_overlaps(first.data(),a.size(),second.data(),b.size(),next.data()))return false;
        gpuPairs.fetch_add(next.size(),std::memory_order_relaxed);
        mask=std::move(next);return true;
    }
private:
    static bool pack(const std::vector<Kernel::Box> &boxes,std::vector<float> &out) {
        out.reserve(boxes.size()*6);
        for(const auto &box:boxes) {
            if(box.isEmpty())return false;
            for(int k=0;k<3;++k)if(box.lo[k]>box.hi[k])return false;
            for(int side=0;side<2;++side)for(int k=0;k<3;++k) {
                const double value=side?box.hi[k]:box.lo[k];
                if(!std::isfinite(value)||std::abs(value)>std::numeric_limits<float>::max())return false;
                // L'espansione di un ulp assicura inclusione anche se cast o GPU
                // arrotondano un subnormale a zero. Nessun fast-math geometrico.
                const float direction=side?std::numeric_limits<float>::infinity():-std::numeric_limits<float>::infinity();
                float packed=std::nextafter(float(value),direction);
                if(std::abs(packed)<std::numeric_limits<float>::min())
                    packed=side?std::numeric_limits<float>::min():-std::numeric_limits<float>::min();
                out.push_back(packed);
            }
        }
        return true;
    }
};
}
std::shared_ptr<Kernel::BoxBatchEvaluator> makeGpuBoxEvaluator() {
    return forgecad_gpu_box_available()?std::make_shared<GpuBoxEvaluator>():nullptr;
}
void initializeGeometryAcceleration() {
    const char *setting=std::getenv("FORGECAD_GPU_BROAD_PHASE");
    Kernel::setBoxBatchEvaluator(setting&&std::strcmp(setting,"1")==0?makeGpuBoxEvaluator():nullptr);
    if(std::getenv("FORGECAD_PROFILE"))std::fprintf(stderr,"PROFILO broad phase: %s, GPU %s\n",forgecad_gpu_box_backend(),
        Kernel::getBoxBatchEvaluator()?"abilitata":"disabilitata (BVH CPU)");
}
std::uint64_t gpuBoxPairsProcessed(){return gpuPairs.load(std::memory_order_relaxed);}
}
