#pragma once
#include "fk_bspline_surface.h"
namespace ForgeCad::Kernel {
// Limiti unilaterali esatti delle basi razionali, con pesi congelati.
// Il segno della riparametrizzazione e' ricavato dal supporto neutro.
struct SurfacePoleJetSide {
    int axis = 0;
    double parameter = 0;
    bool left = false, reversed = false;
};
struct SurfacePoleJetTerm {
    int side, u, v;
    double coefficient;
};
struct SurfacePoleJetRow {
    int order;
    std::vector<SurfacePoleJetTerm> terms;
};
struct SurfacePoleJetLink {
    bool tangent = false, curvature = false, compatible = false;
    bool protectedBand = false;
    std::vector<SurfacePoleJetTerm> support;
    std::vector<SurfacePoleJetRow> rows;
};
SurfacePoleJetLink surfacePoleJetLink(const BSplineSurface &a, SurfacePoleJetSide sa, const BSplineSurface &b,
                                      SurfacePoleJetSide sb, int order);
// Nodi interni che non garantiscono gia' C1/C2 per molteplicita', e cuciture
// geometricamente chiuse. Non ricerca giunti per prossimita' fra facce.
std::vector<SurfacePoleJetLink> surfacePoleInternalJets(const BSplineSurface &surface, int order);
// Residuo relativo dei vincoli sui jet del supporto neutro; non un massimo
// globale della curvatura, ne' una verifica di regolarita' o auto-intersezione.
double surfacePoleJetResidual(const SurfacePoleJetLink &link, const BSplineSurface &a, const BSplineSurface &b,
                              int order);
} // namespace ForgeCad::Kernel
