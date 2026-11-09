#pragma once
#include "cad_surface_poles_model.h"
#include "fk_topology.h"
#include "fk_surface_refit.h"
#include "fk_cancel.h"
#include <QObject>
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
    void cacheRefit(const QVector<Kernel::FaceId> &faces, const std::vector<Kernel::BSplineSurfacePtr> &sources,
                    const Kernel::SurfaceRefitGrid &grid);

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
// Il calcolo non blocca gli eventi del mouse. Ogni nuova richiesta cancella
// quella precedente; la consegna avviene sul thread GUI e solo a oggetto vivo.
class SurfacePolesRefitSearch final : public QObject {
  public:
    explicit SurfacePolesRefitSearch(QObject *parent) : QObject(parent) {}
    ~SurfacePolesRefitSearch() override { cancel(); }
    void cancel();
    void start(std::vector<Kernel::BSplineSurfacePtr> sources, int u, int v, double tolerance);
    std::function<void(const Kernel::SurfaceRefitGrid &, const QString &)> finished;
  private:
    Kernel::CancellationToken cancellation_;
    quint64 generation_ = 0;
};
} // namespace ForgeCad
