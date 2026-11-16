#pragma once
#include "fk_surface_mapping.h"
#include <memory>
namespace ForgeCad::Kernel {
// Corrispondenza comune piano/quilt basata sulle lunghezze delle isocurve.
// Un carrier o due facce tangenti con giunzione isoparametrica U; nessuna
// proiezione per raggi. La mappa non promette isometria su doppia curvatura.
// I corpi destinazione devono restare immutabili e vivi quanto questa mappa.
struct SurfaceFlattenStats {
    double minimumStretch = 1, maximumStretch = 1;
    double minimumAreaRatio = 1, maximumAreaRatio = 1;
    double boundaryDeviation = 0;
    int samples = 0;
};
// Origine deterministica: punto interno di una faccia o centro del bordo
// topologico comune. Non riassocia geometrie per prossimita'.
Vec2 surfaceQuiltAnchor(const std::vector<SurfaceTargetFace> &targets);
class SurfaceQuiltMap {
public:
    SurfaceQuiltMap(const std::vector<SurfaceTargetFace> &targets, const Vec2 &anchor);
    Vec2 flatten(std::size_t patch, const Vec2 &uv) const;
    // Quota lungo la normale orientata, in millimetri. carrierOnly consente
    // la continuazione del carrier per il fit fuori dal materiale sorgente.
    Vec3 fold(const Vec3 &flat, bool carrierOnly = false) const;
    // Appiattimento non isometrico, con bordo comune condiviso e genealogia.
    // Gli estremi delle variazioni metriche sono misure campionate interne
    // al trim, non limiti globali certificati.
    Body flattenSheet(const Frame3 &plane, double tolerance, SurfaceFlattenStats *stats = nullptr,
        BodyHistory *history = nullptr) const;
    Body bend(const Body &source, const SurfaceWrapOptions &options, BodyHistory *history = nullptr,
        double *deviation = nullptr) const;

private:
    struct Data;
    std::shared_ptr<const Data> data_;
};
}
