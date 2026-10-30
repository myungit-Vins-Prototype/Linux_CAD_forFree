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

// Interfaccia storica condivisa dai backend CUDA, Metal e stub CPU.
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

// Buffer di lavoro: somma memoria pinned host e device. Prenotazione prima
// delle allocazioni, inclusa la capacita' eventualmente riusata. Budget o
// numero massimo di richieste raggiunti: evaluate restituisce false senza
// disabilitare CUDA; il chiamante usa la CPU, senza attese per uno slot.
// Superfici caricate, contesto/driver e altri backend esclusi dai budget.
constexpr std::size_t kForgeCudaActiveBudget = 128 * 1024 * 1024;
constexpr std::size_t kForgeCudaMaxActiveSlots = 4;
// Cache separata dei soli buffer inattivi.
// Su Metal i byte contano i buffer condivisi UV/output (64 byte per punto),
// senza memoria pinned o copie device separate. Stessi limiti di budget/slot.
constexpr std::size_t kForgeCudaIdleBudget = 64 * 1024 * 1024;
struct ForgeCudaPoolStats {
    std::size_t activeBytes = 0, idleBytes = 0;
    std::size_t activeSlots = 0, peakActiveBytes = 0, peakActiveSlots = 0;
    std::size_t allocations = 0, reuses = 0, budgetRejections = 0;
};
// Snapshot coerente sotto il mutex; picchi/contatori dall'avvio del processo.
// Byte di capacita' prenotata, non RSS o memoria totale della GPU.
ForgeCudaPoolStats forgecad_cuda_pool_stats();
std::size_t forgecad_cuda_idle_bytes();
// Libera gli slot attualmente inattivi; i worker attivi possono restituirne altri.
void forgecad_cuda_trim_idle();

#endif
