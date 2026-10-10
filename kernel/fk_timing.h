#ifndef FORGECAD_FK_TIMING_H
#define FORGECAD_FK_TIMING_H

#include <chrono>
#include <cstdio>
#include <cstdlib>

// Cronometri delle fasi per la diagnostica dei tempi: attivi solo con la
// variabile d'ambiente FK_PROFILE (stampa su stderr), altrimenti nessun costo
// oltre a una lettura dell'ambiente per cronometro.
namespace ForgeCad::Kernel::detail {

inline bool timingEnabled() {
    static const bool enabled = std::getenv("FK_PROFILE") != nullptr;
    return enabled;
}

class PhaseTimer {
public:
    explicit PhaseTimer(const char *name) : name_(name), start_(std::chrono::steady_clock::now()) {}
    ~PhaseTimer() {
        if (timingEnabled())
            std::fprintf(stderr, "[tempi] %s: %.0f ms\n", name_,
                         std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start_).count());
    }
    PhaseTimer(const PhaseTimer &) = delete;
    PhaseTimer &operator=(const PhaseTimer &) = delete;

private:
    const char *name_;
    std::chrono::steady_clock::time_point start_;
};

}

#endif
