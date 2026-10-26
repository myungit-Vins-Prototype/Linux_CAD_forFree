// Contratto del pool CUDA: limiti prima delle allocazioni, rifiuti temporanei,
// output intatti, riuso e richieste/trim concorrenti. Oracolo piano analitico.
#include "cuda_support.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>

namespace {
int failures = 0;
void check(bool condition, const char *message) {
    if (!condition) { ++failures; std::fprintf(stderr, "FALLITO: %s\n", message); }
}
using SurfaceHandle = std::unique_ptr<void, decltype(&forgecad_cuda_release_surface)>;
SurfaceHandle plane(double z) {
    const double knots[] = {0, 0, 1, 1};
    const double poles[] = {0, 0, z, 0, 1, z + .2, 1, 0, z + .1, 1, 1, z + .3};
    return {forgecad_cuda_upload_surface({1, 1, 2, 2, knots, knots, poles, nullptr}),
            forgecad_cuda_release_surface};
}
bool exactPlane(const std::vector<double> &uv, const std::vector<double> &points,
                const std::vector<double> &normals, double z) {
    const std::array<double, 3> normal = {-.1 / std::sqrt(1.05), -.2 / std::sqrt(1.05), 1. / std::sqrt(1.05)};
    for (std::size_t i = 0; i < uv.size() / 2; ++i) {
        const std::array<double, 3> point = {uv[2*i], uv[2*i+1], z + .1*uv[2*i] + .2*uv[2*i+1]};
        for (std::size_t c = 0; c < 3; ++c)
            if (!std::isfinite(points[3*i+c]) || !std::isfinite(normals[3*i+c]) ||
                std::abs(points[3*i+c] - point[c]) > 1e-12 || std::abs(normals[3*i+c] - normal[c]) > 1e-12) return false;
    }
    return true;
}
bool evaluate(void *handle, double z, std::size_t count) {
    std::vector<double> uv(2*count), points(3*count), normals(3*count);
    for (std::size_t i = 0; i < count; ++i) {
        uv[2*i] = double(i % 257) / 256.;
        uv[2*i+1] = double((i*37) % 257) / 256.;
    }
    return forgecad_cuda_evaluate_surface(handle, uv.data(), count, points.data(), normals.data()) &&
           exactPlane(uv, points, normals, z);
}
bool settled() {
    const auto stats = forgecad_cuda_pool_stats();
    return stats.activeSlots == 0 && stats.activeBytes == 0 && stats.idleBytes <= kForgeCudaIdleBudget &&
           stats.peakActiveBytes <= kForgeCudaActiveBudget && stats.peakActiveSlots <= kForgeCudaMaxActiveSlots;
}
void rejectionAndReuse() {
    forgecad_cuda_trim_idle();
    check(forgecad_cuda_idle_bytes() == 0, "cache inizialmente vuota");
    auto surface = plane(4);
    check(bool(surface), "upload piano");
    if (!surface) return;
    check(evaluate(surface.get(), 4, 1024), "piano prima del rifiuto");
    const auto beforeReuse = forgecad_cuda_pool_stats();
    check(evaluate(surface.get(), 4, 512), "piano con slot riusato");
    const auto before = forgecad_cuda_pool_stats();
    check(before.reuses == beforeReuse.reuses + 1 && before.allocations == beforeReuse.allocations,
          "riuso senza nuove allocazioni");
    // Array validi per l'intera richiesta: il caso e' riproducibile anche sul
    // backend precedente, dove viene calcolato anziche' rifiutato dal budget.
    const auto count = kForgeCudaActiveBudget / (16*sizeof(double)) + 1;
    constexpr double sentinel = -9876.5;
    std::vector<double> uv(2*count, .5), points(3*count, sentinel), normals(3*count, sentinel);
    check(!forgecad_cuda_evaluate_surface(surface.get(), uv.data(), count, points.data(), normals.data()),
          "lotto oltre budget rifiutato");
    const auto after = forgecad_cuda_pool_stats();
    check(after.budgetRejections == before.budgetRejections + 1 && after.allocations == before.allocations,
          "rifiuto prima delle allocazioni");
    check(std::all_of(points.begin(), points.end(), [](double x) { return x == sentinel; }) &&
          std::all_of(normals.begin(), normals.end(), [](double x) { return x == sentinel; }), "output rifiutati intatti");
    check(forgecad_cuda_available() && evaluate(surface.get(), 4, 1024), "CUDA ancora utilizzabile dopo il rifiuto");
    check(settled(), "prenotazioni restituite dopo rifiuto/riuso");
    forgecad_cuda_trim_idle();
    // Lotto ammesso da128MiB esatti, maggiore della cache64MiB: viene liberato.
    check(evaluate(surface.get(), 4, count - 1), "lotto esattamente al limite attivo");
    check(settled() && forgecad_cuda_idle_bytes() == 0, "slot grande liberato e prenotazione restituita");
}
void concurrentRequests(std::size_t count) {
    constexpr int workersCount = 8;
    constexpr double sentinel = -9876.5;
    std::vector<SurfaceHandle> surfaces;
    for (int worker = 0; worker < workersCount; ++worker) surfaces.push_back(plane(worker));
    if (std::any_of(surfaces.begin(), surfaces.end(), [](const auto &s) { return !s; })) {
        check(false, "upload dei piani concorrenti"); return;
    }
    forgecad_cuda_trim_idle();
    std::atomic<int> ready{0}, errors{0}, succeeded{0}, rejected{0};
    std::atomic<bool> start{false}, stopTrim{false};
    std::vector<std::thread> workers;
    for (int worker = 0; worker < workersCount; ++worker) workers.emplace_back([&, worker] {
        std::vector<double> uv(2*count), points(3*count), normals(3*count);
        for (std::size_t i = 0; i < count; ++i) {
            uv[2*i] = double(i % 257) / 256.; uv[2*i+1] = double((i*37) % 257) / 256.;
        }
        ++ready;
        while (!start.load()) std::this_thread::yield();
        for (int repeat = 0; repeat < 4; ++repeat) {
            std::fill(points.begin(), points.end(), sentinel);
            std::fill(normals.begin(), normals.end(), sentinel);
            if (forgecad_cuda_evaluate_surface(surfaces[worker].get(), uv.data(), count, points.data(), normals.data())) {
                ++succeeded;
                if (!exactPlane(uv, points, normals, worker)) ++errors;
            } else {
                ++rejected;
                if (!std::all_of(points.begin(), points.end(), [](double x) { return x == sentinel; }) ||
                    !std::all_of(normals.begin(), normals.end(), [](double x) { return x == sentinel; })) ++errors;
            }
            const auto stats = forgecad_cuda_pool_stats();
            if (stats.activeBytes > kForgeCudaActiveBudget || stats.activeSlots > kForgeCudaMaxActiveSlots ||
                stats.idleBytes > kForgeCudaIdleBudget) ++errors;
        }
    });
    std::thread trimmer([&] {
        while (!start.load()) std::this_thread::yield();
        while (!stopTrim.load()) {
            forgecad_cuda_trim_idle();
            std::this_thread::yield();
        }
    });
    while (ready.load() != workersCount) std::this_thread::yield();
    const auto before = forgecad_cuda_pool_stats();
    start = true;
    for (auto &worker : workers) worker.join();
    stopTrim = true; trimmer.join();
    const auto after = forgecad_cuda_pool_stats();
    check(errors == 0 && succeeded > 0, "richieste concorrenti: risultati analitici o output intatti");
    check(std::size_t(rejected.load()) == after.budgetRejections - before.budgetRejections,
          "tutti i rifiuti concorrenti dipendono dal budget");
    check(settled(), "limiti e restituzione prenotazioni con trim concorrente");
    check(forgecad_cuda_available() && evaluate(surfaces[0].get(), 0, 1024), "CUDA recupera dopo saturazione concorrente");
    std::printf("POOL count=%zu succeeded=%d rejected=%d peak_slots=%zu peak_bytes=%zu\n", count,
                succeeded.load(), rejected.load(), after.peakActiveSlots, after.peakActiveBytes);
}
}
int main(int argc, char **argv) {
    const bool stub = argc == 2 && std::strcmp(argv[1], "--cpu-stub") == 0;
    if (stub) {
        check(!forgecad_cuda_available(), "stub senza CUDA");
        check(!forgecad_cuda_evaluate_surface(nullptr, nullptr, 0, nullptr, nullptr), "stub richiede fallback CPU");
        forgecad_cuda_trim_idle();
        const auto s = forgecad_cuda_pool_stats();
        check(s.activeBytes == 0 && s.idleBytes == 0 && s.activeSlots == 0 && s.peakActiveBytes == 0 &&
              s.peakActiveSlots == 0 && s.allocations == 0 && s.reuses == 0 && s.budgetRejections == 0,
              "stub senza prenotazioni o buffer");
    } else {
        if (!forgecad_cuda_available()) { std::puts("GPU CUDA non disponibile: test saltato"); return 77; }
        rejectionAndReuse();
        // Richieste piccole esercitano il limite di slot; quelle grandi anche
        // la somma dei byte. La sovrapposizione effettiva dipende dallo scheduler.
        concurrentRequests(4096);
        concurrentRequests(600000);
        forgecad_cuda_trim_idle();
        check(forgecad_cuda_idle_bytes() == 0 && settled(), "pool finale svuotato senza lavori attivi");
    }
    if (!failures) std::puts("test pool CUDA superati");
    return failures ? 1 : 0;
}
