#ifndef FORGECAD_FK_PARALLEL_H
#define FORGECAD_FK_PARALLEL_H

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <thread>
#include <vector>

// Esecuzione in parallelo di compiti indipendenti (C++17 puro, std::thread).
namespace ForgeCad::Kernel {

// Numero di thread da usare: `requested` se positivo, altrimenti i core della
// macchina.
inline unsigned threadCount(int requested) {
    if (requested > 0) return unsigned(requested);
    return std::max(1u, std::thread::hardware_concurrency());
}

// Chiama task(i) per i in [0, count) su al piu' `threads` thread (il chiamante
// e' uno di loro). I compiti si prendono in ordine da un contatore comune:
// quelli lunghi non bloccano gli altri. `task` non deve lanciare eccezioni
// (chi puo' fallire le raccoglie per indice e le rilancia dopo, in ordine).
template <class Task>
void parallelFor(std::size_t count, unsigned threads, const Task &task) {
    threads = unsigned(std::min<std::size_t>(threads, count));
    if (threads <= 1) {
        for (std::size_t i = 0; i < count; ++i) task(i);
        return;
    }
    std::atomic<std::size_t> next{0};
    const auto work = [&] {
        for (std::size_t i = next++; i < count; i = next++) task(i);
    };
    std::vector<std::thread> pool;
    pool.reserve(threads - 1);
    for (unsigned k = 1; k < threads; ++k) pool.emplace_back(work);
    work();
    for (std::thread &thread : pool) thread.join();
}

}

#endif
