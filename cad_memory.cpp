#include "cad_memory.h"
#include <cstdlib>
#if defined(__GLIBC__)
#include <malloc.h>
#endif

namespace ForgeCad {
bool releaseUnusedHeapMemory() {
#if defined(__GLIBC__)
    return malloc_trim(0) != 0;
#else
    return false;
#endif
}
}
