#include "gpu_tpms_support.h"
#include "kernel/fk_body_check.h"
#include "kernel/fk_body_io.h"
#include "kernel/fk_primitives.h"
#include "kernel/fk_sheet_gyroid.h"
#include "kernel/fk_tpms_cells.h"

#include <iostream>

using namespace ForgeCad::Kernel;

int main() {
    const auto gpu = ForgeCad::makeCudaTpmsEvaluator(0);
    const std::vector<double> emptyCell(8, 1.0);
    const TpmsGridIndex origin{0,0,0}, unit{1,1,1}, cube{2,2,2};
    std::vector<TpmsCell> actual;
    if (!gpu || !gpu->cells(emptyCell, cube, origin, unit, actual)) return 77;
    if (actual != std::vector<TpmsCell>{0}) return 1;

    // Tutti i casi binari, incluse le selle a parità di valore e i nodi nulli.
    constexpr int nz = 512;
    std::vector<double> field(4 * nz);
    for (int mask = 0; mask < 256; ++mask) {
        for (int i = 0; i < 8; ++i)
            field[8 * mask + i] = mask & (1 << i) ? 1.0 : (i % 2 ? -1.0 : 0.0);
    }
    const TpmsGridIndex size{2,2,nz}, full{1,1,nz-1};
    if (!gpu->cells(field, size, origin, full, actual) || actual.size() != std::size_t(nz - 1)) return 2;
    for (int z = 0; z < full[2]; ++z)
        if (actual[z] != tpmsCellConnections(field.data(), 4 * z, 2, 4)) return 3;
    const TpmsGridIndex first{0,0,1}, last{1,1,nz-2};
    if (!gpu->cells(field, size, first, last, actual) || actual.size() != std::size_t(nz - 3)) return 2;
    for (int z = first[2]; z < last[2]; ++z)
        if (actual[z - first[2]] != tpmsCellConnections(field.data(), 4 * z, 2, 4)) return 3;
    const auto verified = actual;
    if (gpu->cells(field, size, first, size, actual) || actual != verified) return 4;
    const auto autoGpu = ForgeCad::makeCudaTpmsEvaluator(2);
    if (autoGpu->cells(emptyCell, cube, origin, unit, actual) || actual != verified) return 5;

    const Body source = makeCylinder(Frame3(), 1.6, 3.2);
    const SheetGyroid pattern{{0,0,.8}, 2.5, .42};
    const auto envelope = sampleCurvedGyroidEnvelope(source, .125);
    const Body cpu = curvedGyroidSolid(source, *envelope, pattern, 2, .8, 2.45);
    setTpmsBatchEvaluator(gpu);
    const Body accelerated = curvedGyroidSolid(source, *envelope, pattern, 2, .8, 2.45);
    setTpmsBatchEvaluator(nullptr);
    if (writeBodyBinary(cpu) != writeBodyBinary(accelerated) || !checkBody(accelerated, {true,true}).empty()) return 6;
    std::cout << "CUDA: connessioni e B-rep identici alla CPU.\n";
}
