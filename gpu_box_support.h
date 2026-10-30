#ifndef FORGECAD_GPU_BOX_SUPPORT_H
#define FORGECAD_GPU_BOX_SUPPORT_H
#include <cstddef>

// Box float espansi verso l'esterno sulla CPU: sei valori lo.xyz,hi.xyz.
// L'uscita ha firstCount*secondCount byte in ordine first-major. L'API e'
// thread-safe; il chiamante impone un budget di memoria prima dell'allocazione.
const char *forgecad_gpu_box_backend();
bool forgecad_gpu_box_available();
bool forgecad_gpu_box_overlaps(const float *first,std::size_t firstCount,
                               const float *second,std::size_t secondCount,unsigned char *mask);
#endif
