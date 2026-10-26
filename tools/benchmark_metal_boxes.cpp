// Benchmark manuale del filtro Metal: sorgente identica per due librerie.
// Dati sintetici, verifica completa delle maschere fuori timer. Compilare con
// -O3 -std=c++17 -I. e libforgecad_compute.a, framework Metal e Foundation.
// Non appartiene all'applicazione o ai test con soglie temporali in CI.
#include "gpu_box_support.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <thread>
#include <vector>
struct Data {
 std::size_t na,nb;std::vector<float>a,b;std::vector<unsigned char>out,expected;
 Data(std::size_t n,std::size_t m,int offset):na(n),nb(m),a(n*6),b(m*6),out(n*m),expected(n*m){
  for(std::size_t i=0;i<na;++i)for(int k=0;k<3;++k){a[i*6+k]=float(i%128);a[i*6+3+k]=float(i%128)+.5f;}
  for(std::size_t j=0;j<nb;++j)for(int k=0;k<3;++k){b[j*6+k]=float((j+offset)%128)+.25f;b[j*6+3+k]=float((j+offset)%128)+.75f;}
  for(std::size_t i=0;i<na;++i)for(std::size_t j=0;j<nb;++j)expected[i*nb+j]=i%128==(j+offset)%128;
 }
 bool run(){return forgecad_gpu_box_overlaps(a.data(),na,b.data(),nb,out.data());}
};
int main(){if(!forgecad_gpu_box_available())return 77;
 for(int workers:{1,4})for(const auto shape:{std::pair<std::size_t,std::size_t>{256,256},{1024,1024},{4096,4096},{8192,32}}){
  if(workers==4&&(shape.first==256||shape.first==8192))continue;
  std::vector<Data>data;for(int w=0;w<workers;++w)data.emplace_back(shape.first,shape.second,w*11);
  for(auto&d:data)if(!d.run()||d.out!=d.expected)return 1;
  std::vector<double>times;const int repeats=workers==1?31:15;
  for(int r=0;r<repeats;++r){
   double ms=0;
   if(workers==1){auto start=std::chrono::steady_clock::now();if(!data[0].run())return 2;ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();}
   else {
    std::mutex mutex;std::condition_variable ready;int arrived=0;bool go=false;std::atomic<bool>ok{true};std::vector<std::thread>threads;
    for(int w=0;w<workers;++w)threads.emplace_back([&,w]{{std::unique_lock<std::mutex>lock(mutex);++arrived;ready.notify_all();ready.wait(lock,[&]{return go;});}if(!data[w].run())ok=false;});
    std::unique_lock<std::mutex>lock(mutex);ready.wait(lock,[&]{return arrived==workers;});auto start=std::chrono::steady_clock::now();go=true;ready.notify_all();lock.unlock();
    for(auto&t:threads)t.join();ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();if(!ok)return 3;
   }
   for(const auto&d:data)if(d.out!=d.expected)return 4;
   times.push_back(ms);
  }
  auto sorted=times;std::sort(sorted.begin(),sorted.end());
  std::printf("{\"workers\":%d,\"na\":%zu,\"nb\":%zu,\"median_ms\":%.9g,\"min_ms\":%.9g,\"max_ms\":%.9g,\"samples_ms\":[",workers,shape.first,shape.second,sorted[sorted.size()/2],sorted.front(),sorted.back());
  for(std::size_t i=0;i<times.size();++i)std::printf("%s%.9g",i?",":"",times[i]);std::puts("]}");
 }return 0;}
