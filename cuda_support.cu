#include "cuda_support.h"

#include <cuda_runtime.h>

#include <atomic>
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <vector>

// Valutazione delle superfici B-spline/NURBS sulla GPU per la tassellazione di
// display (cad_cuda_tessellation): un thread per punto, in double, con gli
// stessi algoritmi del kernel (findSpan, A2.2 con la derivata prima, A4.4 per
// il quoziente razionale). I risultati servono solo a disegnare: la
// geometria esatta resta quella del kernel sulla CPU.

namespace {

__global__ void forgecad_cuda_probe_kernel() {}

std::atomic<bool> broken{false};  // un errore CUDA spegne la GPU per il resto della sessione

struct DeviceSurface {
    ForgeCudaBSplineSurface surface;  // puntatori in memoria della GPU
    double *block = nullptr;
};

constexpr int kMax = kForgeCudaMaxDegree + 1;

__device__ int findSpan(const double *knots, int degree, int poleCount, double t) {
    const int p = degree, n = poleCount - 1;
    if (t >= knots[n + 1]) {
        int span = n;
        while (knots[span] >= knots[n + 1]) --span;
        return span;
    }
    if (t <= knots[p]) {
        int span = p;
        while (knots[span + 1] <= knots[p]) ++span;
        return span;
    }
    // upper_bound in [p, n + 2) meno uno, come la versione della CPU.
    int lo = p, hi = n + 2;
    while (lo < hi) {
        const int mid = (lo + hi) / 2;
        if (knots[mid] > t) hi = mid;
        else lo = mid + 1;
    }
    return lo - 1;
}

// Funzioni di base non nulle N_{span-p+k, p}(u) e le loro derivate prime.
template<int Degree>
__device__ void basis(const double *knots, int span, double u, int degree, double *N, double *dN) {
    constexpr int capacity=Degree<0?kMax:Degree+1;
    const int p=Degree<0?degree:Degree;
    double left[capacity], right[capacity], previous[capacity];
    N[0] = 1.0;
    for (int j = 1; j <= p; ++j) {
        left[j] = u - knots[span + 1 - j];
        right[j] = knots[span + j] - u;
        if (j == p)
            for (int r = 0; r < p; ++r) previous[r] = N[r];  // grado p - 1
        double saved = 0.0;
        for (int r = 0; r < j; ++r) {
            const double temp = N[r] / (right[r + 1] + left[j - r]);
            N[r] = saved + right[r + 1] * temp;
            saved = left[j - r] * temp;
        }
        N[j] = saved;
    }
    for (int k = 0; k <= p; ++k) {
        double d = 0.0;
        if (p > 0) {
            if (k >= 1) {
                const double span1 = knots[span + k] - knots[span - p + k];
                if (span1 != 0.0) d += previous[k - 1] / span1;
            }
            if (k < p) {
                const double span2 = knots[span + k + 1] - knots[span - p + k + 1];
                if (span2 != 0.0) d -= previous[k] / span2;
            }
            d *= p;
        }
        dN[k] = d;
    }
}

template<int DegreeU=-1,int DegreeV=-1>
__global__ void evaluateKernel(ForgeCudaBSplineSurface s, const double *uv, int count, double *points, double *normals) {
    for (int index = blockIdx.x * blockDim.x + threadIdx.x; index < count; index += blockDim.x * gridDim.x) {
        const double u = uv[2 * index], v = uv[2 * index + 1];
        const int pu = DegreeU<0?s.uDegree:DegreeU, pv = DegreeV<0?s.vDegree:DegreeV;
        const int su = findSpan(s.uKnots, pu, s.uPoleCount, u);
        const int sv = findSpan(s.vKnots, pv, s.vPoleCount, v);
        constexpr int capacityU=DegreeU<0?kMax:DegreeU+1,capacityV=DegreeV<0?kMax:DegreeV+1;
        double nu[capacityU], dnu[capacityU], nv[capacityV], dnv[capacityV];
        basis<DegreeU>(s.uKnots, su, u, pu, nu, dnu);
        basis<DegreeV>(s.vKnots, sv, v, pv, nv, dnv);
        double a[3] = {0, 0, 0}, au[3] = {0, 0, 0}, av[3] = {0, 0, 0};
        double w = 0.0, wu = 0.0, wv = 0.0;
        for (int i = 0; i <= pu; ++i) {
            const int row = su - pu + i;
            for (int j = 0; j <= pv; ++j) {
                const int pole = row * s.vPoleCount + sv - pv + j;
                const double weight = s.weights ? s.weights[pole] : 1.0;
                const double b = nu[i] * nv[j] * weight, bu = dnu[i] * nv[j] * weight, bv = nu[i] * dnv[j] * weight;
                for (int c = 0; c < 3; ++c) {
                    const double x = s.poles[3 * pole + c];
                    a[c] += b * x;
                    au[c] += bu * x;
                    av[c] += bv * x;
                }
                w += b;
                wu += bu;
                wv += bv;
            }
        }
        double p[3], dpu[3], dpv[3];
        for (int c = 0; c < 3; ++c) {
            if (s.weights) {
                p[c] = a[c] / w;
                dpu[c] = (au[c] - wu * p[c]) / w;
                dpv[c] = (av[c] - wv * p[c]) / w;
            } else {
                p[c] = a[c];
                dpu[c] = au[c];
                dpv[c] = av[c];
            }
            points[3 * index + c] = p[c];
        }
        const double n[3] = {dpu[1] * dpv[2] - dpu[2] * dpv[1], dpu[2] * dpv[0] - dpu[0] * dpv[2], dpu[0] * dpv[1] - dpu[1] * dpv[0]};
        const double length = sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        const double scale = fmax(sqrt(dpu[0] * dpu[0] + dpu[1] * dpu[1] + dpu[2] * dpu[2]),
                                  sqrt(dpv[0] * dpv[0] + dpv[1] * dpv[1] + dpv[2] * dpv[2]));
        // Come Surface::normal: sotto kAngularResolution il punto e' singolare.
        const bool regular = length > 1e-11 * scale * scale;
        for (int c = 0; c < 3; ++c) normals[3 * index + c] = regular ? n[c] / length : 0.0;
    }
}

bool deviceReady() {
    static std::once_flag once;
    static bool ready = false;
    std::call_once(once, [] {
        int deviceCount = 0;
        if (cudaGetDeviceCount(&deviceCount) != cudaSuccess || deviceCount == 0) return;
        forgecad_cuda_probe_kernel<<<1, 1>>>();
        ready = cudaDeviceSynchronize() == cudaSuccess;
    });
    return ready && !broken.load();
}

bool fail() {
    cudaGetLastError();
    broken.store(true);
    return false;
}

}

const char *forgecad_cuda_backend() {
    return "CUDA backend (runtime NVIDIA)";
}

bool forgecad_cuda_available() {
    return deviceReady();
}

void *forgecad_cuda_upload_surface(const ForgeCudaBSplineSurface &surface) {
    if (!deviceReady()) return nullptr;
    if (surface.uDegree < 0 || surface.vDegree < 0 || surface.uDegree > kForgeCudaMaxDegree || surface.vDegree > kForgeCudaMaxDegree)
        return nullptr;
    const std::size_t uKnots = std::size_t(surface.uPoleCount + surface.uDegree + 1);
    const std::size_t vKnots = std::size_t(surface.vPoleCount + surface.vDegree + 1);
    const std::size_t poles = std::size_t(surface.uPoleCount) * std::size_t(surface.vPoleCount);
    const std::size_t total = uKnots + vKnots + 3 * poles + (surface.weights ? poles : 0);
    auto *device = new DeviceSurface;
    if (cudaMalloc(&device->block, total * sizeof(double)) != cudaSuccess) {
        delete device;
        fail();
        return nullptr;
    }
    double *cursor = device->block;
    const auto copy = [&](const double *source, std::size_t count) {
        double *target = cursor;
        cursor += count;
        return cudaMemcpy(target, source, count * sizeof(double), cudaMemcpyHostToDevice) == cudaSuccess ? target : nullptr;
    };
    device->surface = surface;
    device->surface.uKnots = copy(surface.uKnots, uKnots);
    device->surface.vKnots = copy(surface.vKnots, vKnots);
    device->surface.poles = copy(surface.poles, 3 * poles);
    device->surface.weights = surface.weights ? copy(surface.weights, poles) : nullptr;
    // Le copie da memoria host paginabile possono tornare prima del completamento
    // sulla GPU. Gli slot usano stream non bloccanti: prima di pubblicare il
    // handle devono essere disponibili anche tutti i poli delle superfici grandi.
    if (!device->surface.uKnots || !device->surface.vKnots || !device->surface.poles || (surface.weights && !device->surface.weights)
        || cudaStreamSynchronize(nullptr) != cudaSuccess) {
        forgecad_cuda_release_surface(device);
        fail();
        return nullptr;
    }
    return device;
}

void forgecad_cuda_release_surface(void *handle) {
    auto *device = static_cast<DeviceSurface *>(handle);
    if (!device) return;
    cudaFree(device->block);
    delete device;
}

namespace {

// Memoria di lavoro riusata tra le chiamate: uno stream, un buffer della CPU
// non paginabile e uno della GPU. Allocare a ogni lotto costava centinaia di
// microsecondi, piu' del calcolo; i thread delle facce prendono uno slot
// libero e lo restituiscono.
struct Slot {
    cudaStream_t stream = nullptr;
    double *host = nullptr, *device = nullptr;
    std::size_t capacity = 0;  // in double
    Slot() = default;
    Slot(const Slot &) = delete;
    Slot &operator=(const Slot &) = delete;

    void releaseBuffers() noexcept {
        if (host) cudaFreeHost(host);
        if (device) cudaFree(device);
        host = device = nullptr;
        capacity = 0;
    }
    ~Slot() {
        releaseBuffers();
        if (stream) cudaStreamDestroy(stream);
    }
};

struct SlotPool {
    std::mutex mutex;
    std::vector<std::unique_ptr<Slot>> freeSlots;
    std::size_t idleBytes = 0;
};

SlotPool &slotPool() {
    // Come prima, la cache resta viva fino al termine del processo. Evita
    // distruttori statici CUDA dopo l'arresto del runtime o con worker attivi;
    // la memoria inattiva e' limitata e puo' essere svuotata esplicitamente.
    static SlotPool *const pool = new SlotPool;
    return *pool;
}

std::unique_ptr<Slot> acquireSlot(std::size_t doubles) {
    std::unique_ptr<Slot> slot;
    {
        auto &pool = slotPool();
        std::lock_guard<std::mutex> lock(pool.mutex);
        if (!pool.freeSlots.empty()) {
            // Preferisci il piu' piccolo slot sufficiente, altrimenti cresci
            // il piu' grande disponibile. Nessuna allocazione sotto il lock.
            auto best = pool.freeSlots.begin();
            for (auto it = pool.freeSlots.begin(); it != pool.freeSlots.end(); ++it) {
                const auto a = (*it)->capacity, b = (*best)->capacity;
                if ((a >= doubles && (b < doubles || a < b)) ||
                    (a < doubles && b < doubles && a > b)) best = it;
            }
            slot = std::move(*best);
            pool.freeSlots.erase(best);
            pool.idleBytes -= 2 * slot->capacity * sizeof(double);
        }
    }
    if (!slot) {
        slot = std::make_unique<Slot>();
        if (cudaStreamCreateWithFlags(&slot->stream, cudaStreamNonBlocking) != cudaSuccess) {
            return nullptr;
        }
    }
    if (slot->capacity < doubles) {
        slot->releaseBuffers();
        const std::size_t capacity = std::max<std::size_t>(doubles, std::size_t(1) << 16);
        if (cudaMallocHost(&slot->host, capacity * sizeof(double)) != cudaSuccess ||
            cudaMalloc(&slot->device, capacity * sizeof(double)) != cudaSuccess) {
            return nullptr;
        }
        slot->capacity = capacity;
    }
    return slot;
}

void releaseSlot(Slot *raw) noexcept {
    std::unique_ptr<Slot> slot(raw);
    if (!slot) return;
    try {
        auto &pool = slotPool();
        std::lock_guard<std::mutex> lock(pool.mutex);
        const auto bytes = 2 * slot->capacity * sizeof(double);
        if (bytes <= kForgeCudaIdleBudget - pool.idleBytes) {
            pool.freeSlots.push_back(std::move(slot));
            pool.idleBytes += bytes;
        }
        // Slot eccedenti distrutti dopo il rilascio del lock.
    } catch (...) {
        // Se il vettore non puo' crescere, distruggi lo slot gia' sincronizzato
        // fuori dal lock. La restituzione non deve lanciare da un distruttore.
    }
}

struct ReturnSlot {
    void operator()(Slot *slot) const noexcept { releaseSlot(slot); }
};

}

std::size_t forgecad_cuda_idle_bytes() {
    auto &pool = slotPool();
    std::lock_guard<std::mutex> lock(pool.mutex);
    return pool.idleBytes;
}

void forgecad_cuda_trim_idle() {
    std::vector<std::unique_ptr<Slot>> retired;
    auto &pool = slotPool();
    {
        std::lock_guard<std::mutex> lock(pool.mutex);
        retired.swap(pool.freeSlots);
        pool.idleBytes = 0;
    }
    // Distruzione CUDA fuori dal lock; nessun buffer attivo viene toccato.
}

bool forgecad_cuda_evaluate_surface(void *handle, const double *uv, std::size_t count, double *points, double *normals) {
    auto *device = static_cast<DeviceSurface *>(handle);
    if (!device || count == 0 || count > std::size_t(1) << 28 || broken.load()) return false;
    const std::unique_ptr<Slot, ReturnSlot> slot(acquireSlot(8 * count).release());
    if (!slot) return fail();
    // Ingresso: (u, v); uscita: punti e normali, consecutivi.
    double *hostUV = slot->host, *hostOut = slot->host + 2 * count;
    double *deviceUV = slot->device, *deviceOut = slot->device + 2 * count;
    std::memcpy(hostUV, uv, 2 * count * sizeof(double));
    bool ok = cudaMemcpyAsync(deviceUV, hostUV, 2 * count * sizeof(double), cudaMemcpyHostToDevice, slot->stream) == cudaSuccess;
    if (ok) {
        const int block = 128;
        const int grid = int((count + block - 1) / block);
        // Gradi comuni: array piccoli e cicli a limite noto, senza cambiare
        // double, ordine delle somme o criterio di singolarita'. Gli altri
        // gradi conservano il percorso generale fino al grado 15.
        const auto &surface=device->surface;
        const char *setting=std::getenv("FORGECAD_CUDA_GENERIC");
        const char *specialized=std::getenv("FORGECAD_CUDA_SPECIALIZED");
        // Sperimentale: il confronto hardware non dimostra ancora un guadagno stabile.
        const bool generic=(setting && std::strcmp(setting,"1")==0)
            || !(specialized && std::strcmp(specialized,"1")==0);
        if(!generic && surface.uDegree==1 && surface.vDegree==1)
            evaluateKernel<1,1><<<grid,block,0,slot->stream>>>(surface,deviceUV,int(count),deviceOut,deviceOut+3*count);
        else if(!generic && surface.uDegree==2 && surface.vDegree==2)
            evaluateKernel<2,2><<<grid,block,0,slot->stream>>>(surface,deviceUV,int(count),deviceOut,deviceOut+3*count);
        else if(!generic && surface.uDegree==3 && surface.vDegree==3)
            evaluateKernel<3,3><<<grid,block,0,slot->stream>>>(surface,deviceUV,int(count),deviceOut,deviceOut+3*count);
        else evaluateKernel<><<<grid,block,0,slot->stream>>>(surface,deviceUV,int(count),deviceOut,deviceOut+3*count);
        ok = cudaGetLastError() == cudaSuccess;
    }
    ok = ok && cudaMemcpyAsync(hostOut, deviceOut, 6 * count * sizeof(double), cudaMemcpyDeviceToHost, slot->stream) == cudaSuccess;
    ok = cudaStreamSynchronize(slot->stream) == cudaSuccess && ok;
    if (ok) {
        std::memcpy(points, hostOut, 3 * count * sizeof(double));
        std::memcpy(normals, hostOut + 3 * count, 3 * count * sizeof(double));
    }
    return ok ? true : fail();
}
