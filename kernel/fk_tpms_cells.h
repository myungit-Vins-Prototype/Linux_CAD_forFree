#ifndef FORGECAD_FK_TPMS_CELLS_H
#define FORGECAD_FK_TPMS_CELLS_H

#include "fk_tpms_batch.h"

#if defined(__CUDACC__)
#define FORGECAD_TPMS_CPU_GPU __host__ __device__
#else
#define FORGECAD_TPMS_CPU_GPU
#endif

namespace ForgeCad::Kernel {

FORGECAD_TPMS_CPU_GPU inline unsigned tpmsCellSigns(const double *field, int first, int strideY, int strideZ) {
    unsigned signs = 0;
    for (int i = 0; i < 8; ++i) {
        const int offset = (i & 1) + ((i >> 1) & 1) * strideY + (i >> 2) * strideZ;
        signs |= unsigned(field[first + offset] > 0.0) << i;
    }
    return signs;
}

FORGECAD_TPMS_CPU_GPU inline TpmsCell tpmsCellConnections(const double *field, int first, int strideY, int strideZ) {
    const auto signs = tpmsCellSigns(field, first, strideY, strideZ);
    if (signs == 0 || signs == 255) return 0;
    const int faceCorners[6][4] = {
        {0,4,6,2}, {1,3,7,5}, {0,1,5,4}, {2,6,7,3}, {0,2,3,1}, {4,5,7,6}
    };
    const int faceEdges[6][4] = {
        {8,6,10,4}, {5,11,7,9}, {0,9,2,8}, {10,3,11,1}, {4,1,5,0}, {2,7,3,6}
    };

    double values[8];
    for (int i = 0; i < 8; ++i)
        values[i] = field[first + (i & 1) + ((i >> 1) & 1) * strideY + (i >> 2) * strideZ];
    TpmsCell connections = 0;
    for (int face = 0; face < 6; ++face) {
        const auto &corner = faceCorners[face];
        const auto &edge = faceEdges[face];
        int hits[4], count = 0;
        for (int i = 0; i < 4; ++i)
            if ((values[corner[i]] > 0.0) != (values[corner[(i + 1) % 4]] > 0.0))
                hits[count++] = i;
        if (count == 0) continue;
        if (count == 4) {
            // La sella bilineare e l'ordine dei nodi sono comuni a entrambi i cubi.
            const double a = values[corner[0]] * values[corner[2]];
            const double b = values[corner[1]] * values[corner[3]];
            const int minA = corner[0] < corner[2] ? corner[0] : corner[2];
            const int minB = corner[1] < corner[3] ? corner[1] : corner[3];
            if (!(a > b || (a == b && minA < minB))) {
                hits[0] = 0; hits[1] = 3;
                hits[2] = 1; hits[3] = 2;
            }
        }
        for (int i = 0; i < count; i += 2) {
            int from = hits[i], to = hits[i + 1];
            if (values[corner[from]] > 0.0) {
                from = hits[i + 1];
                to = hits[i];
            }
            // Quattro bit per lato: zero significa assenza di un successore.
            connections |= TpmsCell(edge[to] + 1) << (4 * edge[from]);
        }
    }
    return connections;
}

inline int tpmsNextEdge(TpmsCell connections, int edge) {
    return int((connections >> (4 * edge)) & 15) - 1;
}

}

#undef FORGECAD_TPMS_CPU_GPU
#endif
