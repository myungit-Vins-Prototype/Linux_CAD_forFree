#pragma once

namespace ForgeCad {
// Rilascio best effort delle pagine libere dell'allocatore, dopo una grande
// riduzione delle mesh. Non libera oggetti vivi o risorse GPU. No-op senza glibc.
bool releaseUnusedHeapMemory();
}
