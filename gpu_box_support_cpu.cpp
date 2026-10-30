#include "gpu_box_support.h"
const char *forgecad_gpu_box_backend(){return "CPU";}
bool forgecad_gpu_box_available(){return false;}
bool forgecad_gpu_box_overlaps(const float *,std::size_t,const float *,std::size_t,unsigned char *){return false;}
