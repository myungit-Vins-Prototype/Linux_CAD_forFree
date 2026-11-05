#ifndef FORGECAD_FK_BSPLINE_ISOLINE_H
#define FORGECAD_FK_BSPLINE_ISOLINE_H

#include "fk_bspline_surface.h"

namespace ForgeCad::Kernel::detail {

// Jet primo della stessa superficie a u fisso. Contrae una volta la base U
// per le colonne V richieste dalla quadratura, senza approssimare la NURBS.
// La superficie deve restare viva e immutata; evaluate e' concorrente.
class BSplineUIsoline {
public:
    BSplineUIsoline(const BSplineSurface &surface, double u, const Interval &vRange);
    // Stesso layout di Surface::evaluate(u, v, 1, out); fuori dalla finestra
    // preparata si usa direttamente la superficie originale.
    void evaluate(double v, Vec3 *out) const;

private:
    struct Column {
        Vec3 origin, value, du;
        double weight = 0.0, duWeight = 0.0;
    };
    const BSplineSurface &placed_, &source_;
    double u_;
    Interval range_;
    int first_;
    std::vector<Column> columns_;
};

}
#endif
