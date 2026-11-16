#ifndef FORGECAD_FK_SWEEP_SURFACE_VALIDATION_H
#define FORGECAD_FK_SWEEP_SURFACE_VALIDATION_H
#include "fk_bspline_surface.h"
#include "fk_precision.h"
#include <cstddef>
#include <string>
#include <vector>
namespace ForgeCad::Kernel::detail {
// Parametri globali del rettangolo: asse u = profilo, v = percorso.
// Ogni family costituisce una griglia rettangolare completa; periodi zero
// per assi aperti. I valori >0 dichiarano cuciture genealogiche esplicite.
struct SweepSurfaceChart {
    std::shared_ptr<const BSplineSurface> surface;
    int family = 0;
    Interval u, v;
    double uPeriod = 0.0, vPeriod = 0.0;
    double tolerance = linearResolution();
    // Bound assoluti per coefficienti di conversioni analitiche certificate.
    double poleError = 0.0, weightError = 0.0;
};
struct SweepSurfaceValidationOptions {
    std::size_t maxVisits = 500000;
    std::size_t maxCells = 200000;
    std::size_t maxCoefficients = 2000000;
    std::size_t maxChartCells = 128;
    int maxDepth = 24;
};
enum class SweepSurfaceValidationStatus { Certified, Unresolved };
struct SweepSurfaceValidationResult {
    SweepSurfaceValidationStatus status = SweepSurfaceValidationStatus::Unresolved;
    std::string diagnostic;
    std::size_t visits = 0, cells = 0;
    int deepestSubdivision = 0;
    double adjacencyDeviation = 0.0;
};
struct SweepSurfaceProjectionBounds {
    bool resolved = false;
    Vec2 lo, hi;
    Interval height;
};
// Ranges nel dominio globale atlas, non parametri locali del carrier.
SweepSurfaceProjectionBounds boundSweepSurfaceProjection(const SweepSurfaceChart &, const Frame3 &, Interval uRange,
                                                         Interval vRange);
enum class SweepSurfaceBoundary { UMin, UMax, VMin, VMax };
struct SweepSurfaceCapContactResult {
    bool confined = false;
    double deviation = 0;
    std::string diagnostic;
};
// Il bordo e' dichiarato dalla genealogia. Certifica che gli eventuali zeri
// dell'altezza sul piano restano entro tolerance fisica dal bordo dichiarato.
SweepSurfaceCapContactResult boundSweepSurfaceCapContact(const SweepSurfaceChart &chart, const Frame3 &plane,
                                                         SweepSurfaceBoundary boundary, Interval uRange,
                                                         Interval vRange, double tolerance);
// Indice di estrazione preparato una volta per carrier. Le query non
// ricostruiscono l'intera superficie a ogni rettangolo cap/side.
class SweepSurfaceQuery {
  public:
    explicit SweepSurfaceQuery(const SweepSurfaceChart &);
    SweepSurfaceProjectionBounds projection(const Frame3 &, Interval uRange, Interval vRange) const;
    SweepSurfaceCapContactResult capContact(const Frame3 &, SweepSurfaceBoundary, Interval uRange, Interval vRange,
                                            double tolerance) const;

  private:
    struct Data;
    std::shared_ptr<Data> data_;
};
// Esclude intersezioni delle superfici reali oltre la tolleranza dichiarata,
// anche all'interno della medesima faccia. Alle cuciture genealogiche il
// bound SPD ammette soltanto deviazione fisica <= tolerance, riportata nel
// risultato; non equivale a iniettivita' IEEE esatta dei carrier discontinui. Non riempie l'inviluppo del profilo. Le
// estrazioni Bezier/suddivisioni e i bound delle derivate sono outward sui coefficienti del carrier. Trim/caps
// richiedono verifiche separate; rettangoli in eccesso restano candidati, mai esclusi per campionamento o per sola
// adiacenza.
SweepSurfaceValidationResult validateSweepSurfaces(const std::vector<SweepSurfaceChart> &charts,
                                                   const SweepSurfaceValidationOptions &options = {});
} // namespace ForgeCad::Kernel::detail
#endif
