#include "gpu_box_support.h"
#include "metal_box_pool.h"
#include <atomic>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

static void check(bool value,const char *message) { if(!value)throw std::runtime_error(message); }
static void evaluate(std::size_t na,std::size_t nb,int offset=0) {
    std::vector<float> a(na*6),b(nb*6);
    std::vector<unsigned char> output(na*nb,0xcc);
    for(std::size_t i=0;i<na;++i)for(int k=0;k<3;++k) {
        a[i*6+k]=float(i%128);a[i*6+3+k]=float(i%128)+.5f;
    }
    for(std::size_t j=0;j<nb;++j)for(int k=0;k<3;++k) {
        b[j*6+k]=float((j+offset)%128)+.25f;b[j*6+3+k]=float((j+offset)%128)+.75f;
    }
    check(forgecad_gpu_box_overlaps(a.data(),na,b.data(),nb,output.data()),"calcolo Metal non eseguito");
    for(std::size_t i=0;i<na;++i)for(std::size_t j=0;j<nb;++j)
        check(output[i*nb+j]==(i%128==(j+offset)%128),"maschera Metal diversa dal riferimento");
}

int main(int argc,char **argv) {
    if(!forgecad_gpu_box_available()) {
        std::cout<<"SKIP Metal non disponibile\n";
        return argc>1&&std::string(argv[1])=="--require-gpu"?1:77;
    }
    try {
        forgecad_metal_box_trim_idle();
        check(forgecad_metal_box_pool_stats().idleBytes==0,"pool inizialmente vuoto");
        evaluate(1024,1024);
        auto before=forgecad_metal_box_pool_stats();
        check(before.idleBytes>0 && before.activeBytes==0,"buffer restituiti al pool");
        evaluate(256,256,7);
        auto after=forgecad_metal_box_pool_stats();
        check(after.allocatedSets==before.allocatedSets && after.reusedSets==before.reusedSets+1,
              "lotto minore riusa il set senza allocazioni");
        evaluate(2048,1024,13);evaluate(1024,1024,31);
        check(forgecad_metal_box_pool_stats().idleBytes<=kMetalBoxIdleBudget,"budget inattivo rispettato");
        before=forgecad_metal_box_pool_stats();
        evaluate(3*1024*1024,1,3); // circa 75 MiB: ammesso attivo, non trattenuto inattivo.
        after=forgecad_metal_box_pool_stats();
        check(after.allocatedSets==before.allocatedSets+1 && after.idleBytes==before.idleBytes,
              "set oltre budget inattivo rilasciato dopo il comando");
        float box[6]={0,0,0,1,1,1};unsigned char mask=0xcc;
        check(!forgecad_gpu_box_overlaps(box,0,box,1,&mask)&&mask==0xcc,"batch vuoto non tocca uscita");
        check(!forgecad_gpu_box_overlaps(nullptr,1,box,1,&mask)&&mask==0xcc,"puntatore nullo non tocca uscita");
        check(!forgecad_gpu_box_overlaps(box,4097,box,4097,&mask)&&mask==0xcc,"maschera oltre limite rifiutata");
        check(!forgecad_gpu_box_overlaps(box,16*1024*1024,box,1,&mask)&&mask==0xcc,
              "limite memoria attiva validato prima di leggere input");

        std::mutex mutex;std::condition_variable ready;int arrived=0;bool go=false;
        std::atomic<int> failures{0},done{0};std::vector<std::thread> workers;
        for(int worker=0;worker<4;++worker)workers.emplace_back([&,worker] {
            {std::unique_lock<std::mutex> lock(mutex);++arrived;ready.notify_all();ready.wait(lock,[&]{return go;});}
            try {evaluate(4096,4096,worker*11);}catch(...) {++failures;}
            ++done;
        });
        {std::unique_lock<std::mutex> lock(mutex);ready.wait(lock,[&]{return arrived==4;});go=true;ready.notify_all();}
        // Lo svuotamento non deve invalidare buffer usati dai comandi in volo.
        while(done<4) {forgecad_metal_box_trim_idle();std::this_thread::yield();}
        for(auto &worker:workers)worker.join();
        check(failures==0,"richieste concorrenti corrotte o rifiutate");
        after=forgecad_metal_box_pool_stats();
        check(after.peakActiveCalls>1 && after.peakActiveCalls<=kMetalBoxMaxActiveCalls,"richieste indipendenti realmente sovrapposte");
        check(after.activeBytes==0 && after.activeCalls==0 && after.idleBytes<=kMetalBoxIdleBudget,
              "tutti i lease restituiti entro i budget");
        forgecad_metal_box_trim_idle();check(forgecad_metal_box_pool_stats().idleBytes==0,"svuotamento finale");
        std::cout<<"PASS Metal pool: allocated_sets="<<after.allocatedSets<<" reused_sets="<<after.reusedSets
                 <<" completed="<<after.completedCalls<<" peak_active="<<after.peakActiveCalls<<'\n';
    }catch(const std::exception &error) {std::cerr<<error.what()<<'\n';return 1;}
    return 0;
}
