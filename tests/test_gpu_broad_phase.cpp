#include "cad_gpu_broad_phase.h"
#include "gpu_box_support.h"
#include "fk_boolean.h"
#include "fk_body_check.h"
#include "fk_extrude.h"
#include "fk_mass.h"
#include "fk_cancel.h"
#include "fk_transform.h"
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <random>

using namespace ForgeCad;
using namespace ForgeCad::Kernel;
static void check(bool value,const char *message){if(!value)throw std::runtime_error(message);}
static Box box(double lo,double hi){Box b;b.add(Vec3(lo,lo,lo));b.add(Vec3(hi,hi,hi));return b;}

static Body prism() {
    ProfileRegion region;constexpr int sides=256;
    for(int i=0;i<sides;++i){
        const double t=2*kPi*i/sides,u=2*kPi*(i+1)/sides;
        const Vec2 p(10*std::cos(t),10*std::sin(t)),q(10*std::cos(u),10*std::sin(u));
        region.outer.segments.push_back({std::make_shared<Line<2>>(p,q-p),{0,distance(p,q)}});
    }
    return makeExtrusion(Frame3(),region,10);
}

static void fallbackTests() {
    struct Evaluator : BoxBatchEvaluator {
        int mode,calls=0;explicit Evaluator(int value):mode(value){}
        bool overlaps(const std::vector<Box> &a,const std::vector<Box> &b,std::vector<unsigned char> &mask) override {
            ++calls;mask.assign(a.size()*b.size(),mode==0?1:0);
            if(mode==1)return false;
            if(mode==2)throw std::runtime_error("errore simulato dispositivo");
            if(mode==3){mask.clear();return true;}
            if(mode==4)throw OperationCancelled{};
            return true;
        }
    };
    const auto a=prism(),b=transformBody(a,Transform3::translation(Vec3(19,0,0)));
    setBoxBatchEvaluator(nullptr);const auto reference=booleanOperation(a,b,BooleanOperation::Subtract);
    const auto expected=massProperties(reference).volume;
    for(int mode=0;mode<5;++mode) {
        auto evaluator=std::make_shared<Evaluator>(mode);setBoxBatchEvaluator(evaluator);
        bool cancelled=false;
        try {
            const auto result=booleanOperation(a,b,BooleanOperation::Subtract);
            check(checkBody(result).empty()&&std::abs(massProperties(result).volume-expected)<1e-8,"fallback modifica geometria");
        }catch(const OperationCancelled &){cancelled=true;}
        check(evaluator->calls>0,"fallback non esercitato");
        check(cancelled==(mode==4),"annullamento non deve essere convertito in fallback");
    }
    setBoxBatchEvaluator(nullptr);std::cout<<"PASS CPU broad-phase fallback/error/cancellation\n";
}

int main(int argc,char **argv) {
    try {
        if(argc>1&&std::string(argv[1])=="--cpu-fallback"){fallbackTests();return 0;}
        auto gpu=makeGpuBoxEvaluator();
        if(!gpu){std::cout<<"SKIP GPU unavailable ("<<forgecad_gpu_box_backend()<<")\n";return argc>1&&std::string(argv[1])=="--require-gpu"?1:77;}
        std::vector<Box> a,b;
        for(int i=0;i<128;++i){a.push_back(box(i*.1,i*.1+.05));b.push_back(box(i*.1+.025,i*.1+.075));}
        std::mt19937 random(20261025);
        std::uniform_real_distribution<double> coordinate(-10,10),extent(.001,2);
        for(int i=0;i<128;++i){
            Box left,right;
            for(int k=0;k<3;++k){left.lo[k]=coordinate(random);left.hi[k]=left.lo[k]+extent(random);
                right.lo[k]=coordinate(random);right.hi[k]=right.lo[k]+extent(random);}
            a.push_back(left);b.push_back(i%4?right:left);
        }
        // Esatti contatti double, separazioni inferiori a un ulp float,
        // subnormali, cambi di segno e coordinate lontane dall'origine.
        for(double value:{0.0,1.0,-1.0,1e-40,-1e-40,1e6,-1e6,1e30}) {
            const double next=std::nextafter(value,std::numeric_limits<double>::infinity());
            a.push_back(box(value,next));b.push_back(box(next,next));
        }
        std::vector<unsigned char> mask;
        check(gpu->overlaps(a,b,mask),"GPU batch non eseguito");
        std::size_t retained=0;
        for(std::size_t i=0;i<a.size();++i)for(std::size_t j=0;j<b.size();++j){
            const bool exact=a[i].overlaps(b[j]);
            check(!exact||mask[i*b.size()+j],"GPU ha escluso un contatto esatto");
            retained+=mask[i*b.size()+j]&&exact;
        }
        const auto previous=mask;
        check(!gpu->overlaps({Box{}},b,mask)&&mask==previous,"errore non consegna maschera parziale");
        check(!gpu->overlaps({box(1e100,2e100)},b,mask)&&mask==previous,"overflow float usa fallback");
        check(!gpu->overlaps({},b,mask)&&mask==previous,"batch vuoto usa fallback");
        check(!gpu->overlaps(std::vector<Box>(4097,box(0,1)),std::vector<Box>(4097,box(0,1)),mask)
            && mask==previous,"budget superato usa fallback senza allocare la maschera");
        const Body first=prism();
        const Body second=transformBody(first,Transform3::translation(Vec3(19,0,0)));
        setBoxBatchEvaluator(nullptr);
        auto start=std::chrono::steady_clock::now();
        const Body reference=booleanOperation(first,second,BooleanOperation::Subtract);
        const double cpuMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        const auto before=gpuBoxPairsProcessed();
        setBoxBatchEvaluator(gpu);start=std::chrono::steady_clock::now();
        const Body result=booleanOperation(first,second,BooleanOperation::Subtract);
        const double gpuMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        setBoxBatchEvaluator(nullptr);
        check(gpuBoxPairsProcessed()>before,"booleana non ha eseguito il filtro GPU");
        check(checkBody(result).empty(),"booleana GPU invalida");
        const auto expected=massProperties(reference),actual=massProperties(result);
        check(std::abs(expected.volume-actual.volume)<1e-8,"volume differente fra CPU e GPU");
        check(reference.faces().size()==result.faces().size()&&reference.edges().size()==result.edges().size(),"topologia differente fra CPU e GPU");
        std::cout<<"PASS "<<forgecad_gpu_box_backend()<<" batch_pairs="<<a.size()*b.size()<<" retained="<<retained
                 <<" boolean_cpu_ms="<<cpuMs<<" boolean_gpu_ms="<<gpuMs<<" gpu_pairs="<<gpuBoxPairsProcessed()<<'\n';
    }catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
    return 0;
}
