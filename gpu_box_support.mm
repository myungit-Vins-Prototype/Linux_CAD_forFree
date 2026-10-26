#include "gpu_box_support.h"
#include "metal_box_pool.h"
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <cstring>
#include <mutex>
#include <algorithm>
#include <array>
#include <optional>

namespace {
struct Buffers {
    id<MTLBuffer> a = nil, b = nil, output = nil;
    std::size_t aBytes = 0, bBytes = 0, outputBytes = 0;
    std::size_t bytes() const { return aBytes + bBytes + outputBytes; }
    bool fits(const Buffers &needed) const {
        return aBytes >= needed.aBytes && bBytes >= needed.bBytes && outputBytes >= needed.outputBytes;
    }
};
struct MetalBoxes {
    id<MTLDevice> device;
    id<MTLCommandQueue> queue;
    id<MTLComputePipelineState> pipeline;
    std::mutex mutex;
    std::array<std::optional<Buffers>, kMetalBoxMaxActiveCalls> idle;
    MetalBoxPoolStats stats;
    MetalBoxes() {
        @autoreleasepool {
            device=MTLCreateSystemDefaultDevice();
            if(!device)return;
            NSString *source=@"#include <metal_stdlib>\nusing namespace metal;\n"
                "kernel void overlap(device const float* a[[buffer(0)]],device const float* b[[buffer(1)]],"
                "device uchar* result[[buffer(2)]],constant uint2& sizes[[buffer(3)]],uint index[[thread_position_in_grid]]) {"
                "if(index>=sizes.x*sizes.y)return;uint ia=index/sizes.y,ib=index%sizes.y;bool hit=true;"
                "for(uint k=0;k<3;++k)hit=hit&&(a[ia*6+k]<=b[ib*6+3+k])&&(b[ib*6+k]<=a[ia*6+3+k]);"
                "result[index]=hit?1:0;}";
            MTLCompileOptions *options=[MTLCompileOptions new];
            if(@available(macOS 15.0,*)) options.mathMode=MTLMathModeSafe;
            else {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
                options.fastMathEnabled=NO;
#pragma clang diagnostic pop
            }
            NSError *error=nil;
            id<MTLLibrary> library=[device newLibraryWithSource:source options:options error:&error];
            id<MTLFunction> function=[library newFunctionWithName:@"overlap"];
            if(function)pipeline=[device newComputePipelineStateWithFunction:function error:&error];
            if(pipeline)queue=[device newCommandQueue];
        }
    }
};
MetalBoxes &state(){static MetalBoxes boxes;return boxes;}

// Ogni chiamata possiede i suoi buffer fino alla conclusione del comando.
// Il mutex protegge soltanto prenotazione, contatori e cache, mai l'attesa GPU.
struct Lease {
    MetalBoxes &gpu;
    Buffers buffers;
    bool reserved = false, completed = false;

    explicit Lease(MetalBoxes &value) : gpu(value) {}
    Lease(const Lease &) = delete;
    Lease &operator=(const Lease &) = delete;
    bool acquire(const Buffers &needed) {
        std::lock_guard<std::mutex> lock(gpu.mutex);
        if (gpu.stats.activeCalls >= kMetalBoxMaxActiveCalls) {
            ++gpu.stats.rejectedCalls;return false;
        }
        auto best = gpu.idle.end();
        for (auto it = gpu.idle.begin();it != gpu.idle.end();++it)
            if (*it && (*it)->fits(needed)
                && (*it)->bytes() <= kMetalBoxActiveBudget - gpu.stats.activeBytes
                && (best == gpu.idle.end() || (*it)->bytes() < (*best)->bytes())) best = it;
        if (best != gpu.idle.end()) {
            buffers = std::move(**best);best->reset();
            gpu.stats.idleBytes -= buffers.bytes();++gpu.stats.reusedSets;
        } else {
            if (needed.bytes() > kMetalBoxActiveBudget - gpu.stats.activeBytes) {
                ++gpu.stats.rejectedCalls;return false;
            }
            buffers = needed;
        }
        gpu.stats.activeBytes += buffers.bytes();
        ++gpu.stats.activeCalls;
        gpu.stats.peakActiveCalls = std::max(gpu.stats.peakActiveCalls,gpu.stats.activeCalls);
        reserved = true;return true;
    }
    ~Lease() {
        if (!reserved) return;
        // L'eventuale deallocazione avviene dopo il rilascio del mutex.
        std::lock_guard<std::mutex> lock(gpu.mutex);
        gpu.stats.activeBytes -= buffers.bytes();--gpu.stats.activeCalls;
        if (completed) {
            ++gpu.stats.completedCalls;
            if (buffers.bytes() <= kMetalBoxIdleBudget - gpu.stats.idleBytes)
                for (auto &slot : gpu.idle) if (!slot) {
                    gpu.stats.idleBytes += buffers.bytes();slot = std::move(buffers);break;
                }
        } else ++gpu.stats.rejectedCalls;
    }
};
std::size_t rounded(std::size_t bytes) { return (bytes + 4095) / 4096 * 4096; }
}
const char *forgecad_gpu_box_backend(){return "Metal";}
bool forgecad_gpu_box_available(){return state().queue!=nil;}
bool forgecad_gpu_box_overlaps(const float *first,std::size_t na,const float *second,std::size_t nb,unsigned char *mask) {
    constexpr std::size_t budget=16*1024*1024;
    if(!first||!second||!mask||!na||!nb||na>budget/nb)return false;
    Buffers needed;
    needed.aBytes=rounded(na*6*sizeof(float));needed.bBytes=rounded(nb*6*sizeof(float));
    needed.outputBytes=rounded(na*nb);
    if(needed.bytes()>kMetalBoxActiveBudget)return false;
    auto &gpu=state();if(!gpu.queue)return false;
    @autoreleasepool {
        Lease lease(gpu);if(!lease.acquire(needed))return false;
        auto &buffers=lease.buffers;
        if(!buffers.a) {
            buffers.a=[gpu.device newBufferWithLength:buffers.aBytes options:MTLResourceStorageModeShared];
            buffers.b=[gpu.device newBufferWithLength:buffers.bBytes options:MTLResourceStorageModeShared];
            buffers.output=[gpu.device newBufferWithLength:buffers.outputBytes options:MTLResourceStorageModeShared];
            if(!buffers.a||!buffers.b||!buffers.output)return false;
            std::lock_guard<std::mutex> lock(gpu.mutex);++gpu.stats.allocatedSets;
        }
        std::memcpy(buffers.a.contents,first,na*6*sizeof(float));
        std::memcpy(buffers.b.contents,second,nb*6*sizeof(float));
        id<MTLCommandBuffer> command=[gpu.queue commandBuffer];
        id<MTLComputeCommandEncoder> encoder=[command computeCommandEncoder];
        if(!command||!encoder)return false;
        [encoder setComputePipelineState:gpu.pipeline];
        [encoder setBuffer:buffers.a offset:0 atIndex:0];[encoder setBuffer:buffers.b offset:0 atIndex:1];
        [encoder setBuffer:buffers.output offset:0 atIndex:2];
        const unsigned sizes[2]={unsigned(na),unsigned(nb)};
        [encoder setBytes:sizes length:sizeof(sizes) atIndex:3];
        const auto width=std::min<NSUInteger>(256,gpu.pipeline.maxTotalThreadsPerThreadgroup);
        [encoder dispatchThreads:MTLSizeMake(na*nb,1,1) threadsPerThreadgroup:MTLSizeMake(width,1,1)];
        [encoder endEncoding];[command commit];[command waitUntilCompleted];
        if(command.status!=MTLCommandBufferStatusCompleted)return false;
        std::memcpy(mask,buffers.output.contents,na*nb);lease.completed=true;return true;
    }
}

MetalBoxPoolStats forgecad_metal_box_pool_stats() {
    auto &gpu=state();std::lock_guard<std::mutex> lock(gpu.mutex);return gpu.stats;
}
void forgecad_metal_box_trim_idle() {
    auto &gpu=state();decltype(gpu.idle) released;
    {std::lock_guard<std::mutex> lock(gpu.mutex);released.swap(gpu.idle);gpu.stats.idleBytes=0;}
}
