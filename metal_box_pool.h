#pragma once

#include <cstddef>
#include <cstdint>

// Diagnostica del solo backend Metal. I budget contano le lunghezze dei
// buffer condivisi, non la memoria del driver o il picco RSS del processo.
constexpr std::size_t kMetalBoxIdleBudget = 64 * 1024 * 1024;
constexpr std::size_t kMetalBoxActiveBudget = 128 * 1024 * 1024;
constexpr std::size_t kMetalBoxMaxActiveCalls = 4;

struct MetalBoxPoolStats {
    std::size_t idleBytes = 0, activeBytes = 0, activeCalls = 0, peakActiveCalls = 0;
    std::uint64_t allocatedSets = 0, reusedSets = 0, completedCalls = 0, rejectedCalls = 0;
};

MetalBoxPoolStats forgecad_metal_box_pool_stats();
// Libera solo i buffer attualmente inattivi; lavori in corso possono
// restituire nuovi buffer al pool dopo questa chiamata.
void forgecad_metal_box_trim_idle();
