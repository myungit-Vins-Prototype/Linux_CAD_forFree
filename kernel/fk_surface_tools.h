#pragma once
#include "fk_boundary.h"
#include "fk_bspline_surface.h"
#include "fk_history.h"
namespace ForgeCad::Kernel {
// Rete ordinata di due famiglie aperte: un solo incrocio 3D per coppia.
// Ogni cella e' una patch di Coons; continuita' interna C0, non G1/G2 implicita.
Body curveNetworkSheet(const std::vector<PathSegment> &rows, const std::vector<PathSegment> &columns, double tolerance = 1e-6,
                       FaceAliases *labels = nullptr);
// Prolunga un bordo libero e rifila sul riferimento entro la distanza massima.
// Il riferimento deve dividere l'estensione senza dividere la superficie iniziale.
Body extendSheetTo(const Body &sheet, EdgeId edge, const Body &tool, double maximum, bool linear = false, double tolerance = 1e-6,
                   BodyHistory *history = nullptr);
struct SurfacePoleChange {
    int u = 0, v = 0;
    Vec3 delta;
    double weightFactor = 1;
};
// Singola faccia NURBS: conserva topologia e trim UV (fori compresi), ricalcola
// curve 3D e vertici. Le modifiche sono relative al supporto parametrico a monte.
Body editSheetPoles(const Body &sheet, const std::vector<SurfacePoleChange> &changes, double tolerance = 1e-6,
                    BodyHistory *history = nullptr);
} // namespace ForgeCad::Kernel
