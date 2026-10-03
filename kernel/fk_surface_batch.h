#ifndef FORGECAD_FK_SURFACE_BATCH_H
#define FORGECAD_FK_SURFACE_BATCH_H

#include <cstddef>

#include "fk_surface.h"

// Valutazione a lotti di punti e normali di una superficie, SOLO per la
// tassellazione di display (fk_tessellate). Il kernel resta C++17 puro: un
// acceleratore esterno (nell'app la GPU con CUDA) implementa l'interfaccia e
// prende le superfici che sa valutare; per le altre restituisce false e la
// tassellazione le valuta sulla CPU come sempre. Nessun calcolo esatto del
// kernel (booleane, misure, proiezioni) passa da qui.
namespace ForgeCad::Kernel {

class SurfaceBatchEvaluator {
public:
    virtual ~SurfaceBatchEvaluator() = default;

    // Punti S(u, v) e normali unitarie Su x Sv di `count` parametri. false se
    // la superficie (o il lotto) non e' gestita: niente e' scritto e vale la
    // CPU. Dove la normale non e' definita (poli, punti singolari) va scritto
    // il vettore nullo: la tassellazione la ricalcola sulla CPU.
    // Chiamata anche da piu' thread insieme (facce in parallelo).
    virtual bool evaluate(const Surface &surface, const Vec2 *uv, std::size_t count, Vec3 *points, Vec3 *normals) = 0;
};

}

#endif
