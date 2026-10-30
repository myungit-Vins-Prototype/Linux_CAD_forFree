#include "cad_cuda_tessellation.h"
#include "cuda_support.h"
#include "fk_bspline_surface.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <random>
#include <thread>

using namespace ForgeCad;
using namespace ForgeCad::Kernel;
namespace {
int failures=0;
void check(bool value,const char *message) { if(!value) { ++failures;std::fprintf(stderr,"FALLITO: %s\n",message); } }
std::vector<double> knots(int degree,int count) {
    std::vector<double> values(std::size_t(degree+1),2.0);
    for(int i=1;i<count-degree;++i)values.push_back(2.0+3.0*i/(count-degree));
    values.insert(values.end(),std::size_t(degree+1),5.0);return values;
}
BSplineSurface surface(int pu,int pv,bool rational,double offset=0) {
    const int nu=pu+4,nv=pv+5;
    std::vector<Vec3> poles;std::vector<double> weights;
    for(int i=0;i<nu;++i)for(int j=0;j<nv;++j) {
        poles.emplace_back(offset+3*i+.2*std::sin(j),offset+2*j+.1*std::cos(i),offset+std::sin(.4*i)*std::cos(.3*j));
        if(rational)weights.push_back(.5+.1*((i*7+j*3)%17));
    }
    return BSplineSurface(pu,pv,knots(pu,nu),knots(pv,nv),nu,nv,std::move(poles),std::move(weights));
}
double worstPoint=0,worstNormal=0;
bool compare(SurfaceBatchEvaluator &gpu,const BSplineSurface &s,std::size_t count) {
    std::mt19937 rng(1741);std::uniform_real_distribution<double> random(2,5);
    std::vector<Vec2> uv(count);std::vector<Vec3> points(count),normals(count);
    for(std::size_t i=0;i<count;++i)uv[i]=Vec2(random(rng),random(rng));
    for(std::size_t i=0;i<std::min<std::size_t>(count,4);++i)uv[i]=Vec2(i&1?2:5,i&2?2:5);
    std::size_t at=4;
    for(double u:s.uKnots())if(u>=2 && u<=5 && at<count)uv[at++]=Vec2(u,3.3);
    for(double v:s.vKnots())if(v>=2 && v<=5 && at<count)uv[at++]=Vec2(3.3,v);
    if(!gpu.evaluate(s,uv.data(),count,points.data(),normals.data()))return false;
    for(std::size_t i=0;i<count;++i) {
        const auto expected=s.point(uv[i][0],uv[i][1]);
        if(!isFinite(points[i])||!isFinite(normals[i]))return false;
        const double error=distance(points[i],expected)/std::max(1.,norm(expected));
        worstPoint=std::max(worstPoint,error);
        double normalError=0;
        try { normalError=distance(normals[i],s.normal(uv[i][0],uv[i][1])); }catch(const std::exception &){}
        worstNormal=std::max(worstNormal,normalError);
        if(error>2e-12 || normalError>2e-8) {
            std::fprintf(stderr,"grado %d/%d uv %.17g %.17g CPU %.17g %.17g %.17g Metal %.17g %.17g %.17g error %.3g normal %.3g\n",s.uDegree(),s.vDegree(),uv[i][0],uv[i][1],expected[0],expected[1],expected[2],points[i][0],points[i][1],points[i][2],error,normalError);
            return false;
        }
    }
    return true;
}
void poolAndFallback() {
    forgecad_cuda_trim_idle();
    auto s=surface(3,3,true);auto evaluator=makeTessellationAccelerator();
    check(evaluator && compare(*evaluator,s,4096),"slot iniziale");
    const auto before=forgecad_cuda_pool_stats();
    check(evaluator && compare(*evaluator,s,2048),"riuso slot");
    const auto reused=forgecad_cuda_pool_stats();
    check(reused.reuses>before.reuses && reused.allocations==before.allocations,"buffer condivisi riutilizzati");
    const double k[]={0,0,1,1},p[]={0,0,0,0,1,0,1,0,0,1,1,0};
    ForgeCudaBSplineSurface input{1,1,2,2,k,k,p,nullptr};
    void *handle=forgecad_cuda_upload_surface(input);
    check(handle!=nullptr,"upload piano diretto");
    const std::size_t huge=kForgeCudaActiveBudget/(8*2*sizeof(float))+1;
    double uv[]={.5,.5},out[]={-7,-7,-7},normal[]={-8,-8,-8};
    check(!forgecad_cuda_evaluate_surface(handle,uv,huge,out,normal) && out[0]==-7 && normal[0]==-8,"budget rifiutato prima di leggere input/output");
    const double badUv[]={NAN,.5};
    check(!forgecad_cuda_evaluate_surface(handle,badUv,1,out,normal) && out[0]==-7,"parametri non finiti atomici");
    check(forgecad_cuda_evaluate_surface(handle,uv,1,out,normal) && std::abs(out[0]-.5)<1e-12,"backend funzionante dopo rifiuto");
    forgecad_cuda_release_surface(handle);
    const double tinyKnots[]={0,0,1e-14,1,1};
    input.uKnots=tinyKnots;input.uPoleCount=3;
    // Intervallo fuori precisione Metal: rifiuto prima di leggere i poli.
    check(!forgecad_cuda_upload_surface(input),"nodi mal condizionati tornano alla CPU");
    forgecad_cuda_trim_idle();
    const auto stats=forgecad_cuda_pool_stats();
    check(stats.activeBytes==0 && stats.activeSlots==0 && stats.idleBytes==0 && stats.peakActiveBytes<=kForgeCudaActiveBudget,"rilascio e budget del pool");
}
void concurrent() {
    std::atomic<int> errors{0};std::vector<std::thread> workers;
    for(int t=0;t<4;++t)workers.emplace_back([&,t] {
        auto s=surface(3,2,t&1,1000*t);auto gpu=makeTessellationAccelerator();
        for(int repeat=0;repeat<3;++repeat) {
            std::vector<Vec2> uv(2048,Vec2(2.8,3.1));std::vector<Vec3> points(uv.size()),normals(uv.size());
            if(!gpu || !gpu->evaluate(s,uv.data(),uv.size(),points.data(),normals.data())) { ++errors;continue; }
            for(const auto &p:points)if(!isFinite(p)||distance(p,s.point(2.8,3.1))>1e-9)++errors;
        }
    });
    for(auto &thread:workers)thread.join();check(errors==0,"upload/calcolo concorrenti senza contaminazione");
}
void benchmark() {
    for(std::size_t count:{std::size_t(2048),std::size_t(4096),std::size_t(65536)}) {
        auto s=surface(3,3,true);auto gpu=makeTessellationAccelerator();
        std::vector<Vec2> uv(count);std::vector<Vec3> points(count),normals(count);
        for(std::size_t i=0;i<count;++i)uv[i]=Vec2(2+3.*i/count,2+3.*((i*37)%count)/count);
        check(gpu && gpu->evaluate(s,uv.data(),count,points.data(),normals.data()),"warmup benchmark");
        std::vector<double> cpuTimes,gpuTimes;
        for(int repeat=0;repeat<5;++repeat)for(int mode:{repeat%2,1-repeat%2}) {
            const auto begin=std::chrono::steady_clock::now();
            if(mode)check(gpu->evaluate(s,uv.data(),count,points.data(),normals.data()),"benchmark GPU");
            else for(std::size_t i=0;i<count;++i) { points[i]=s.point(uv[i][0],uv[i][1]);normals[i]=s.normal(uv[i][0],uv[i][1]); }
            const auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
            (mode?gpuTimes:cpuTimes).push_back(elapsed);
        }
        std::sort(cpuTimes.begin(),cpuTimes.end());std::sort(gpuTimes.begin(),gpuTimes.end());
        std::printf("BENCH count=%zu cpu_ms=%.6f metal_ms=%.6f\n",count,cpuTimes[2],gpuTimes[2]);
    }
}
}
int main(int argc,char **argv) {
    // La copertura numerica deve esercitare anche lotti sotto la soglia
    // automatica dell'app, sia da CTest sia nel lancio diretto del runner.
    if(!std::getenv("FORGECAD_METAL_MIN_BATCH"))setenv("FORGECAD_METAL_MIN_BATCH","2048",1);
    bool required=false,bench=false;
    for(int i=1;i<argc;++i) { required|=std::strcmp(argv[i],"--require-gpu")==0;bench|=std::strcmp(argv[i],"--benchmark")==0; }
    if(!cudaTessellationAvailable()) { std::puts("Metal non disponibile");return required?1:77; }
    if(bench) { benchmark();return failures?1:0; }
    auto gpu=makeTessellationAccelerator();check(bool(gpu),"acceleratore Metal");if(!gpu)return 1;
    for(int pu:{1,2,3,5,7,15})for(int pv:{1,2,3,4})for(bool rational:{false,true}) {
        auto s=surface(pu,pv,rational);auto evaluator=makeTessellationAccelerator();
        check(evaluator && compare(*evaluator,s,2048),"superficie polinomiale/razionale e bordi");
    }
    { auto s=surface(3,3,true,1e-8);auto evaluator=makeTessellationAccelerator();check(evaluator && compare(*evaluator,s,2048),"coordinate traslate"); }
    for(bool rational:{false,true}) {
        std::vector<Vec3> poles;std::vector<double> weights;
        for(int i=0;i<7;++i)for(int j=0;j<7;++j) { poles.emplace_back(i,j,std::sin(.3*i)*std::cos(.4*j));if(rational)weights.push_back(.5+.1*((i+j)%9)); }
        BSplineSurface s(3,2,{2,2,2,2,3.5,3.5,3.5,5,5,5,5},{2,2,2,3,3,4,4,5,5,5},7,7,std::move(poles),std::move(weights));
        auto evaluator=makeTessellationAccelerator();check(evaluator && compare(*evaluator,s,2048),"nodi interni multipli");
    }
    for(double offset:{1e8,1e12}) {
        auto s=surface(3,3,true,offset);auto evaluator=makeTessellationAccelerator();
        Vec2 uv(3,3);Vec3 point(-7,-7,-7),normal(-8,-8,-8);
        check(evaluator && !evaluator->evaluate(s,&uv,1,&point,&normal) && point[0]==-7 && normal[0]==-8,"coordinate mal condizionate: fallback atomico");
    }
    poolAndFallback();concurrent();
    // Il percorso completo di rifilatura/tassellazione è coperto dal runner export.
    check(cudaTessellationPoints()>0,"contatore punti GPU effettivi");
    setCudaTessellationEnabled(false);check(!makeTessellationAccelerator(),"interruttore CPU");setCudaTessellationEnabled(true);
    std::printf("Metal punti=%llu scarto relativo punti=%.3g normali=%.3g\n",static_cast<unsigned long long>(cudaTessellationPoints()),worstPoint,worstNormal);
    return failures?1:0;
}
