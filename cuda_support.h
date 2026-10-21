#ifndef FORGECAD_CUDA_SUPPORT_H
#define FORGECAD_CUDA_SUPPORT_H

#include <cstddef>

const char *forgecad_cuda_backend();
bool forgecad_cuda_available();

// Superficie B-spline/NURBS per la GPU (stessa disposizione di
// Kernel::BSplineSurface: nodi espansi, poli [i * vPoleCount + j] come x, y, z
// consecutivi, pesi nella stessa disposizione o nullptr se polinomiale).
struct ForgeCudaBSplineSurface {
    int uDegree, vDegree;
    int uPoleCount, vPoleCount;
    const double *uKnots;  // uPoleCount + uDegree + 1
    const double *vKnots;  // vPoleCount + vDegree + 1
    const double *poles;
    const double *weights;
};

// Grado massimo valutato sulla GPU (oltre resta la CPU).
constexpr int kForgeCudaMaxDegree = 15;

// Copia della superficie in memoria della GPU; nullptr se CUDA non c'e' o la
// superficie non e' gestita. Va liberata con forgecad_cuda_release_surface.
void *forgecad_cuda_upload_surface(const ForgeCudaBSplineSurface &surface);
void forgecad_cuda_release_surface(void *handle);

// Punti (x, y, z) e normali unitarie Su x Sv (x, y, z; nulle nei punti
// singolari) di `count` parametri (u, v) consecutivi, in double. false se il
// calcolo non riesce (la tassellazione torna alla CPU). Si puo' chiamare da
// piu' thread: ognuno usa il suo stream.
bool forgecad_cuda_evaluate_surface(void *handle, const double *uv, std::size_t count, double *points, double *normals);

#endif
