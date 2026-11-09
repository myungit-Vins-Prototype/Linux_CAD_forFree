#pragma once
#include "cad_surface_poles_model.h"
#include "fk_topology.h"
namespace ForgeCad {
// Stato del pannello: tutte le facce selezionate, cache di ricostruzione e
// conversione delle sole modifiche sparse nella definizione persistente.
class SurfacePolesSession {
  public:
    QString store(const SurfacePolesModel &model, SurfaceToolParameters &parameters) const;
    QString load(const ForgeBody &body, const QVector<Kernel::FaceId> &faces, SurfaceToolParameters &parameters,
                 SurfacePolesModel &model);
    double deviation() const { return deviation_; }
    QString details() const { return details_; }
    void reset() { cache_.clear(); }

  private:
    struct Fit {
        Kernel::BSplineSurfacePtr source, surface;
        int u = 0, v = 0;
        double deviation = 0;
    };
    QMap<int, Fit> cache_;
    QVector<Kernel::FaceId> faces_;
    bool legacy_ = true;
    double deviation_ = 0;
    QString details_;
};
} // namespace ForgeCad
