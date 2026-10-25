#include "gpu_box_support.h"
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <cstring>
#include <mutex>
#include <algorithm>

namespace {
struct MetalBoxes {
    id<MTLDevice> device;
    id<MTLCommandQueue> queue;
    id<MTLComputePipelineState> pipeline;
    std::mutex mutex;
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
}
const char *forgecad_gpu_box_backend(){return "Metal";}
bool forgecad_gpu_box_available(){return state().queue!=nil;}
bool forgecad_gpu_box_overlaps(const float *first,std::size_t na,const float *second,std::size_t nb,unsigned char *mask) {
    constexpr std::size_t budget=16*1024*1024;
    if(!first||!second||!mask||!na||!nb||na>budget/nb)return false;
    auto &gpu=state();if(!gpu.queue)return false;
    std::lock_guard<std::mutex> lock(gpu.mutex);
    @autoreleasepool {
        id<MTLBuffer> a=[gpu.device newBufferWithBytes:first length:na*6*sizeof(float) options:MTLResourceStorageModeShared];
        id<MTLBuffer> b=[gpu.device newBufferWithBytes:second length:nb*6*sizeof(float) options:MTLResourceStorageModeShared];
        id<MTLBuffer> output=[gpu.device newBufferWithLength:na*nb options:MTLResourceStorageModeShared];
        if(!a||!b||!output)return false;
        id<MTLCommandBuffer> command=[gpu.queue commandBuffer];
        id<MTLComputeCommandEncoder> encoder=[command computeCommandEncoder];
        if(!command||!encoder)return false;
        [encoder setComputePipelineState:gpu.pipeline];
        [encoder setBuffer:a offset:0 atIndex:0];[encoder setBuffer:b offset:0 atIndex:1];
        [encoder setBuffer:output offset:0 atIndex:2];
        const unsigned sizes[2]={unsigned(na),unsigned(nb)};
        [encoder setBytes:sizes length:sizeof(sizes) atIndex:3];
        const auto width=std::min<NSUInteger>(256,gpu.pipeline.maxTotalThreadsPerThreadgroup);
        [encoder dispatchThreads:MTLSizeMake(na*nb,1,1) threadsPerThreadgroup:MTLSizeMake(width,1,1)];
        [encoder endEncoding];[command commit];[command waitUntilCompleted];
        if(command.status!=MTLCommandBufferStatusCompleted)return false;
        std::memcpy(mask,output.contents,na*nb);return true;
    }
}
