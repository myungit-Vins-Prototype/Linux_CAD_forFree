#pragma once
#include <cstdint>

namespace ForgeCad {
// Stima prudente della memoria disponibile; zero se non determinabile.
std::uint64_t availableMemoryBytes();
// Rilascio best effort delle pagine libere dell'allocatore, dopo una grande
// riduzione delle mesh. Non libera oggetti vivi o risorse GPU. No-op senza glibc.
bool releaseUnusedHeapMemory();
}
