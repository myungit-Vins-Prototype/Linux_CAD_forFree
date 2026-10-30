#include "cad_cuda_tessellation.h"
#include "cuda_support.h"
#include <cstdio>

namespace { bool available=false; }
const char *forgecad_cuda_backend() { return "backend di prova"; }
bool forgecad_cuda_available() { return available; }
void *forgecad_cuda_upload_surface(const ForgeCudaBSplineSurface &) { return nullptr; }
void forgecad_cuda_release_surface(void *) {}
bool forgecad_cuda_evaluate_surface(void *,const double *,std::size_t,double *,double *) { return false; }

int main() {
    using namespace ForgeCad;
    setCudaTessellationEnabled(true);
    if(cudaTessellationAvailable() || makeTessellationAccelerator())return 1;
    available=true;
    if(!cudaTessellationAvailable() || !makeTessellationAccelerator())return 1;
    available=false; // errore del backend dopo l'inizializzazione riuscita
    if(cudaTessellationAvailable() || makeTessellationAccelerator())return 1;
    available=true;setCudaTessellationEnabled(false);
    if(!cudaTessellationAvailable() || makeTessellationAccelerator())return 1;
    std::puts("PASS: disponibilita dinamica e fallback CPU dopo errore");
    return 0;
}
