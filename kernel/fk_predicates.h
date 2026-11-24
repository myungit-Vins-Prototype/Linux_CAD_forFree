#ifndef FORGECAD_FK_PREDICATES_H
#define FORGECAD_FK_PREDICATES_H

#include "fk_math.h"
#include <cstddef>
#include <optional>

// Predicati geometrici esatti (Shewchuk, "Adaptive Precision Floating-Point
// Arithmetic and Fast Robust Geometric Predicates", 1997): un filtro in
// virgola mobile con limite d'errore garantito e, solo se il segno e'
// incerto, il calcolo esatto con espansioni di double. Il segno restituito e'
// esatto per qualunque input finito (senza overflow/underflow).
namespace ForgeCad::Kernel {

// +1 se a, b, c sono in senso antiorario (c a sinistra di a->b),
// -1 se in senso orario, 0 se allineati.
int orient2d(const Vec2 &a, const Vec2 &b, const Vec2 &c);

// +1 se d sta dalla parte della normale (b - a) x (c - a) del piano abc,
// -1 dalla parte opposta, 0 se i quattro punti sono complanari.
int orient3d(const Vec3 &a, const Vec3 &b, const Vec3 &c, const Vec3 &d);

namespace detail {
// Exact Cartesian value of a rational Bézier at a binary64 parameter in
// (0,1). Returns empty outside the bounded expansion regime or when the
// final Cartesian value cannot be proved to equal two binary64 values.
std::optional<Vec2> exactRationalBezierPoint(const Vec2 *poles, const double *weights,
                                            std::size_t count, double parameter);
}

}

#endif
