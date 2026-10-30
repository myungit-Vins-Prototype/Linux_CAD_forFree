#include "gpu_box_support.h"
#include <cuda_runtime.h>
#include <mutex>

namespace {
__global__ void overlapBoxes(const float *a,const float *b,std::size_t count,std::size_t nb,unsigned char *out) {
    const std::size_t index=std::size_t(blockIdx.x)*blockDim.x+threadIdx.x;
    if(index>=count)return;
    const std::size_t ia=index/nb,ib=index%nb;bool hit=true;
    for(int k=0;k<3;++k)hit=hit&&a[ia*6+k]<=b[ib*6+3+k]&&b[ib*6+k]<=a[ia*6+3+k];
    out[index]=hit?1:0;
}
}
const char *forgecad_gpu_box_backend(){return "CUDA";}
bool forgecad_gpu_box_available(){int count=0;return cudaGetDeviceCount(&count)==cudaSuccess&&count>0;}
bool forgecad_gpu_box_overlaps(const float *first,std::size_t na,const float *second,std::size_t nb,unsigned char *mask) {
    constexpr std::size_t budget=16*1024*1024;
    if(!first||!second||!mask||!na||!nb||na>budget/nb)return false;
    // Buffer locali e stream dedicato: nessuno stato o slot trattenuto fra lavori.
    float *a=nullptr,*b=nullptr;unsigned char *out=nullptr;cudaStream_t stream=nullptr;
    bool ok=cudaStreamCreateWithFlags(&stream,cudaStreamNonBlocking)==cudaSuccess;
    if(ok)ok=cudaMalloc(reinterpret_cast<void **>(&a),na*6*sizeof(float))==cudaSuccess;
    if(ok)ok=cudaMalloc(reinterpret_cast<void **>(&b),nb*6*sizeof(float))==cudaSuccess;
    if(ok)ok=cudaMalloc(reinterpret_cast<void **>(&out),na*nb)==cudaSuccess;
    if(ok)ok=cudaMemcpyAsync(a,first,na*6*sizeof(float),cudaMemcpyHostToDevice,stream)==cudaSuccess;
    if(ok)ok=cudaMemcpyAsync(b,second,nb*6*sizeof(float),cudaMemcpyHostToDevice,stream)==cudaSuccess;
    if(ok){overlapBoxes<<<unsigned((na*nb+255)/256),256,0,stream>>>(a,b,na*nb,nb,out);ok=cudaGetLastError()==cudaSuccess;}
    if(ok)ok=cudaMemcpyAsync(mask,out,na*nb,cudaMemcpyDeviceToHost,stream)==cudaSuccess;
    if(stream)ok=(cudaStreamSynchronize(stream)==cudaSuccess)&&ok;
    cudaFree(out);cudaFree(b);cudaFree(a);if(stream)cudaStreamDestroy(stream);
    return ok;
}
