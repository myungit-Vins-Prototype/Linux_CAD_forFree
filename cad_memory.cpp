#include "cad_memory.h"
#include <cstdlib>
#include <fstream>
#include <string>
#include <algorithm>
#if defined(__APPLE__)
#include <mach/mach.h>
#endif
#if defined(__GLIBC__)
#include <malloc.h>
#endif

namespace ForgeCad {
std::uint64_t availableMemoryBytes() {
#if defined(__linux__)
    std::ifstream info("/proc/meminfo");
    std::string key,line;
    std::uint64_t available=0;
    while(info>>key){
        if(key=="MemAvailable:"){info>>available;available=std::max<std::uint64_t>(1,available*1024);break;}
        std::getline(info,line);
    }
    // I limiti del contenitore prevalgono sulla memoria libera dell'host.
    std::ifstream groups("/proc/self/cgroup");
    while(std::getline(groups,line))if(line.rfind("0::",0)==0){
        const auto path=std::string("/sys/fs/cgroup")+line.substr(3);
        std::uint64_t maximum=0,used=0;
        std::ifstream maxFile(path+"/memory.max"),usedFile(path+"/memory.current");
        if((maxFile>>maximum)&&(usedFile>>used))available=std::min(available,maximum>used?maximum-used:1);
    }
    return available;
#elif defined(__APPLE__)
    const auto host=mach_host_self();vm_size_t page=0;vm_statistics64_data_t stats{};
    mach_msg_type_number_t count=HOST_VM_INFO64_COUNT;
    const bool ok=host_page_size(host,&page)==KERN_SUCCESS
        &&host_statistics64(host,HOST_VM_INFO64,reinterpret_cast<host_info64_t>(&stats),&count)==KERN_SUCCESS;
    mach_port_deallocate(mach_task_self(),host);
    return ok?(std::uint64_t(stats.free_count)+stats.inactive_count)*page:0;
#else
    return 0;
#endif
}

bool releaseUnusedHeapMemory() {
#if defined(__GLIBC__)
    return malloc_trim(0) != 0;
#else
    return false;
#endif
}
}
