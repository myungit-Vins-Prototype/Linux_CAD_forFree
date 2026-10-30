#include "cuda_support.h"
#include "metal_surface_kernel.h"
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

// Stessa interfaccia dell'acceleratore CUDA, con memoria condivisa Metal e
// aritmetica hi/lo float. Soltanto punti/normali di display; B-rep invariato.
namespace {
struct DS { float hi,lo; };
DS split(double x) { const float hi=float(x);return {hi,float(x-double(hi))}; }
double join(DS x) { return double(x.hi)+double(x.lo); }
struct Params { unsigned pu,pv,nu,nv,uOffset,vOffset,poleOffset,weightOffset,rational,count; };
struct Surface {
    id<MTLBuffer> data=nil;
    Params params{};
    std::array<double,3> origin{};
    double scale=1,uStart=0,uLength=1,vStart=0,vLength=1;
    std::size_t reservedBytes=0;
    ~Surface();
};
struct Buffers {
    id<MTLBuffer> uv=nil,output=nil;
    std::size_t capacity=0;
    std::size_t bytes() const { return capacity*8*sizeof(DS); }
};
struct Backend {
    id<MTLDevice> device=nil;
    id<MTLCommandQueue> queue=nil;
    id<MTLComputePipelineState> pipeline=nil;
    std::atomic<bool> broken{false};
    std::mutex mutex;
    std::array<std::optional<Buffers>,kForgeCudaMaxActiveSlots> idle;
    ForgeCudaPoolStats stats;
    std::size_t surfaceBytes=0;
    Backend() {
        @autoreleasepool {
            device=MTLCreateSystemDefaultDevice();if(!device)return;
            MTLCompileOptions *options=[MTLCompileOptions new];
            if(@available(macOS 15.0,*)) {
                options.mathMode=MTLMathModeSafe;
                options.mathFloatingPointFunctions=MTLMathFloatingPointFunctionsPrecise;
            }
            else {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
                options.fastMathEnabled=NO;
#pragma clang diagnostic pop
            }
            NSError *error=nil;
            id<MTLLibrary> library=[device newLibraryWithSource:[NSString stringWithUTF8String:kMetalSurfaceKernel] options:options error:&error];
            id<MTLFunction> function=[library newFunctionWithName:@"surfaces"];
            if(function)pipeline=[device newComputePipelineStateWithFunction:function error:&error];
            if(pipeline)queue=[device newCommandQueue];
            if(!queue && error)std::fprintf(stderr,"Tassellazione Metal: %s\n",error.localizedDescription.UTF8String);
        }
    }
    bool ready() const { return queue && !broken.load(); }
};
Backend &backend() { static Backend state;return state; }
Surface::~Surface() {
    if(reservedBytes) { auto &gpu=backend();std::lock_guard<std::mutex> lock(gpu.mutex);gpu.surfaceBytes-=reservedBytes; }
}
struct Lease {
    Backend &gpu;
    Buffers buffers;
    bool reserved=false,completed=false;
    explicit Lease(Backend &state):gpu(state){}
    bool acquire(std::size_t count) {
        std::lock_guard<std::mutex> lock(gpu.mutex);
        auto &stats=gpu.stats;
        if(count>kForgeCudaActiveBudget/(8*sizeof(DS)) || stats.activeSlots>=kForgeCudaMaxActiveSlots) {
            ++stats.budgetRejections;return false;
        }
        auto best=gpu.idle.end();
        for(auto it=gpu.idle.begin();it!=gpu.idle.end();++it)
            if(*it && (*it)->capacity>=count && (*it)->bytes()<=kForgeCudaActiveBudget-stats.activeBytes &&
               (best==gpu.idle.end() || (*it)->capacity<(*best)->capacity))best=it;
        if(best!=gpu.idle.end()) {
            buffers=std::move(**best);best->reset();stats.idleBytes-=buffers.bytes();++stats.reuses;
        } else {
            buffers.capacity=count;
            if(buffers.bytes()>kForgeCudaActiveBudget-stats.activeBytes) { ++stats.budgetRejections;return false; }
        }
        stats.activeBytes+=buffers.bytes();++stats.activeSlots;
        stats.peakActiveBytes=std::max(stats.peakActiveBytes,stats.activeBytes);
        stats.peakActiveSlots=std::max(stats.peakActiveSlots,stats.activeSlots);
        reserved=true;return true;
    }
    ~Lease() {
        if(!reserved)return;
        std::lock_guard<std::mutex> lock(gpu.mutex);
        auto &stats=gpu.stats;stats.activeBytes-=buffers.bytes();--stats.activeSlots;
        if(!completed || buffers.bytes()>kForgeCudaIdleBudget-stats.idleBytes)return;
        for(auto &slot:gpu.idle)if(!slot) { stats.idleBytes+=buffers.bytes();slot=std::move(buffers);break; }
    }
};
bool validKnots(const double *knots,int count,int degree,double &start,double &length) {
    if(!knots)return false;
    start=knots[degree];length=knots[count]-start;
    if(!std::isfinite(start)||!std::isfinite(length)||length<=0)return false;
    for(int i=0;i<count+degree+1;++i) {
        if(!std::isfinite(knots[i]) || (i && knots[i]<knots[i-1]))return false;
        // Intervalli troppo piccoli rispetto al dominio restano in double CPU.
        if(i && knots[i]>knots[i-1] && (knots[i]-knots[i-1])/length<1e-10)return false;
        if(std::abs((knots[i]-start)/length)>1e8)return false;
    }
    return true;
}
}

const char *forgecad_cuda_backend() { return "Metal"; }
bool forgecad_cuda_available() { return backend().ready(); }

void *forgecad_cuda_upload_surface(const ForgeCudaBSplineSurface &input) {
    @autoreleasepool {
        auto &gpu=backend();if(!gpu.ready())return nullptr;
        if(input.uDegree<0 || input.vDegree<0 || input.uDegree>kForgeCudaMaxDegree || input.vDegree>kForgeCudaMaxDegree ||
           input.uPoleCount<=input.uDegree || input.vPoleCount<=input.vDegree || !input.poles)return nullptr;
        const auto count=std::size_t(input.uPoleCount)*std::size_t(input.vPoleCount);
        if(count>64*1024*1024/(4*sizeof(DS)))return nullptr;
        auto surface=std::make_unique<Surface>();
        if(!validKnots(input.uKnots,input.uPoleCount,input.uDegree,surface->uStart,surface->uLength) ||
           !validKnots(input.vKnots,input.vPoleCount,input.vDegree,surface->vStart,surface->vLength))return nullptr;
        double weightScale=1;
        if(input.weights) {
            double lo=std::numeric_limits<double>::max(),hi=0;
            for(std::size_t i=0;i<count;++i) { double w=input.weights[i];if(!std::isfinite(w)||w<=0)return nullptr;lo=std::min(lo,w);hi=std::max(hi,w); }
            if(hi/lo>1e8)return nullptr;
            weightScale=hi;
        }
        double extent=0;
        for(int c=0;c<3;++c)surface->origin[c]=input.poles[c];
        for(std::size_t i=0;i<3*count;++i) {
            if(!std::isfinite(input.poles[i]))return nullptr;
            extent=std::max(extent,std::abs(input.poles[i]-surface->origin[i%3]));
        }
        if(!std::isfinite(extent))return nullptr;
        // A coordinate enormi rispetto alla patch, il kernel double perde
        // precisione nelle derivate per cancellazione. Conserva quel percorso
        // invece di produrre normali diverse con la normalizzazione locale.
        double offset=0;for(double value:surface->origin)offset=std::max(offset,std::abs(value));
        if(extent>0 && offset/extent>1e6)return nullptr;
        surface->scale=extent>0?extent:1;
        auto &p=surface->params;p.pu=unsigned(input.uDegree);p.pv=unsigned(input.vDegree);p.nu=unsigned(input.uPoleCount);p.nv=unsigned(input.vPoleCount);p.rational=input.weights?1:0;
        const std::size_t uk=std::size_t(input.uPoleCount+input.uDegree+1),vk=std::size_t(input.vPoleCount+input.vDegree+1);
        std::vector<DS> data;data.reserve(uk+vk+count*(input.weights?4:3));
        p.uOffset=unsigned(data.size());for(std::size_t i=0;i<uk;++i)data.push_back(split((input.uKnots[i]-surface->uStart)/surface->uLength));
        p.vOffset=unsigned(data.size());for(std::size_t i=0;i<vk;++i)data.push_back(split((input.vKnots[i]-surface->vStart)/surface->vLength));
        p.poleOffset=unsigned(data.size());for(std::size_t i=0;i<3*count;++i)data.push_back(split((input.poles[i]-surface->origin[i%3])/surface->scale));
        p.weightOffset=unsigned(data.size());if(input.weights)for(std::size_t i=0;i<count;++i)data.push_back(split(input.weights[i]/weightScale));
        {
            std::lock_guard<std::mutex> lock(gpu.mutex);
            const std::size_t bytes=data.size()*sizeof(DS),limit=128*1024*1024;
            if(bytes>limit-gpu.surfaceBytes)return nullptr;
            surface->reservedBytes=bytes;gpu.surfaceBytes+=bytes;
        }
        surface->data=[gpu.device newBufferWithBytes:data.data() length:data.size()*sizeof(DS) options:MTLResourceStorageModeShared];
        return surface->data?surface.release():nullptr;
    }
}
void forgecad_cuda_release_surface(void *handle) { delete static_cast<Surface *>(handle); }

bool forgecad_cuda_evaluate_surface(void *handle,const double *uv,std::size_t count,double *points,double *normals) {
    @autoreleasepool {
        auto &gpu=backend();const auto *surface=static_cast<const Surface *>(handle);
        if(!surface || !gpu.ready())return false;
        if(!count)return true;
        if(!uv||!points||!normals||count>std::numeric_limits<unsigned>::max())return false;
        Lease lease(gpu);if(!lease.acquire(count))return false;
        auto &b=lease.buffers;
        if(!b.uv) {
            b.uv=[gpu.device newBufferWithLength:2*b.capacity*sizeof(DS) options:MTLResourceStorageModeShared];
            b.output=[gpu.device newBufferWithLength:6*b.capacity*sizeof(DS) options:MTLResourceStorageModeShared];
            if(!b.uv||!b.output)return false;
            std::lock_guard<std::mutex> lock(gpu.mutex);++gpu.stats.allocations;
        }
        auto *input=static_cast<DS *>(b.uv.contents);
        for(std::size_t i=0;i<count;++i) {
            const double u=(uv[2*i]-surface->uStart)/surface->uLength,v=(uv[2*i+1]-surface->vStart)/surface->vLength;
            if(!std::isfinite(u)||!std::isfinite(v)||u<0||u>1||v<0||v>1)return false;
            input[2*i]=split(u);input[2*i+1]=split(v);
        }
        Params params=surface->params;params.count=unsigned(count);
        id<MTLCommandBuffer> command=[gpu.queue commandBuffer];
        id<MTLComputeCommandEncoder> encoder=[command computeCommandEncoder];
        if(!encoder)return false;
        [encoder setComputePipelineState:gpu.pipeline];[encoder setBuffer:surface->data offset:0 atIndex:0];
        [encoder setBuffer:b.uv offset:0 atIndex:1];[encoder setBuffer:b.output offset:0 atIndex:2];
        [encoder setBytes:&params length:sizeof(params) atIndex:3];
        const NSUInteger width=std::min<NSUInteger>(128,gpu.pipeline.maxTotalThreadsPerThreadgroup);
        [encoder dispatchThreads:MTLSizeMake(count,1,1) threadsPerThreadgroup:MTLSizeMake(width,1,1)];
        [encoder endEncoding];[command commit];[command waitUntilCompleted];
        if(command.status!=MTLCommandBufferStatusCompleted) { gpu.broken.store(true);return false; }
        const auto *output=static_cast<const DS *>(b.output.contents);
        // Valida l'intero lotto prima di toccare gli output del chiamante.
        for(std::size_t i=0;i<6*count;++i)if(!std::isfinite(join(output[i])))return false;
        for(std::size_t i=0;i<count;++i)for(int c=0;c<3;++c)
            if(!std::isfinite(surface->origin[c]+surface->scale*join(output[6*i+c])))return false;
        for(std::size_t i=0;i<count;++i) {
            double n[3];for(int c=0;c<3;++c)n[c]=join(output[6*i+3+c]);
            const double length=std::hypot(n[0],n[1],n[2]);
            for(int c=0;c<3;++c) {
                points[3*i+c]=surface->origin[c]+surface->scale*join(output[6*i+c]);
                normals[3*i+c]=length>0?n[c]/length:0;
            }
        }
        lease.completed=true;return true;
    }
}
ForgeCudaPoolStats forgecad_cuda_pool_stats() { auto &g=backend();std::lock_guard<std::mutex> lock(g.mutex);return g.stats; }
std::size_t forgecad_cuda_idle_bytes() { return forgecad_cuda_pool_stats().idleBytes; }
void forgecad_cuda_trim_idle() { auto &g=backend();std::lock_guard<std::mutex> lock(g.mutex);for(auto &slot:g.idle)slot.reset();g.stats.idleBytes=0; }
