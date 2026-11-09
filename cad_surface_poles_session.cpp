#include "cad_surface_poles_session.h"
#include "cad_surface_tools.h"
#include "cad_topology_ref.h"
#include "fk_surface_refit.h"
#include <climits>
#include "fk_precision.h"
#include <QPointer>
#include <QCoreApplication>
#include <QRunnable>
#include <QThreadPool>
namespace ForgeCad {
QString SurfacePolesSession::store(const SurfacePolesModel &model, SurfaceToolParameters &p) const {
    if (faces_.isEmpty() || !model.rowCount())
        return {};
    if (legacy_)
        return model.edits(p.edits);
    auto output = p.poleFaces;
    int count = 0;
    for (int i = 0; i < faces_.size(); ++i) {
        if (i >= output.size())
            return QStringLiteral("Selezione delle facce da aggiornare.");
        const auto error = model.faceEdits(i, output[i].edits);
        if (!error.isEmpty())
            return QStringLiteral("F%1: %2").arg(faces_[i].index).arg(error);
        count += output[i].edits.size();
    }
    if (count > 4096)
        return QStringLiteral("Modifica fino a 4096 poli totali per funzione.");
    p.poleFaces = std::move(output);
    return {};
}
QString SurfacePolesSession::load(const ForgeBody &body, const QVector<Kernel::FaceId> &faces, SurfaceToolParameters &p,
                                  SurfacePolesModel &model) {
    faces_.clear();
    model.setSurface({});
    deviation_ = 0;
    details_.clear();
    if (faces.isEmpty())
        return QStringLiteral("Scegli almeno una faccia NURBS.");
    if (faces.size() > 256)
        return QStringLiteral("Scegli fino a 256 facce NURBS.");
    legacy_ = body->faces().size() == 1 && p.poleFaces.isEmpty();
    QVector<SurfacePoleFace> records;
    QVector<SurfacePolesModel::FaceGrid> grids;
    qint64 rows = 0;
    try {
        for (const auto face : faces) {
            const auto basis = surfaceBasisHash(body, face);
            if (basis.isEmpty())
                return QStringLiteral("La faccia F%1 non e' NURBS.").arg(face.index);
            SurfacePoleFace entry{faceReference(*body, face, {}), basis, {}};
            if (legacy_) {
                entry.basis = p.basis.isEmpty() ? basis : p.basis;
                entry.edits = p.edits;
            } else
                for (const auto &prior : p.poleFaces) {
                    const auto resolved=resolveSurfacePoleFace(*body,prior.face);
                    if (!resolved.valid())
                        return QStringLiteral(
                            "Una faccia selezionata non e' piu' disponibile: riselezionare le facce.");
                    if (resolved == face) {
                        entry = prior;
                        entry.face = faceReference(*body, face, {});
                    }
                }
            if (entry.basis != basis)
                return QStringLiteral("La griglia F%1 a monte e' cambiata: ricaricare la griglia.").arg(face.index);
            auto surface = std::dynamic_pointer_cast<const Kernel::BSplineSurface>(body->face(face).surface);
            double error = 0;
            if (p.refitU || p.refitV) {
                auto &fit = cache_[face.index];
                if (fit.source != surface || fit.u != p.refitU || fit.v != p.refitV) {
                    const auto result = Kernel::refitSurfacePoles(*surface, p.refitU, p.refitV);
                    fit = {surface, result.surface, p.refitU, p.refitV, result.sampledDeviation};
                }
                surface = fit.surface;
                error = fit.deviation;
                deviation_ = std::max(deviation_, error);
            }
            if (surface->uPoleCount() > 4096 || surface->vPoleCount() > 4096)
                return QStringLiteral("F%1: oltre 4096 poli per direzione; attivare Semplifica la griglia.")
                    .arg(face.index);
            rows += qint64(surface->uPoleCount()) * surface->vPoleCount();
            if (rows > INT_MAX)
                return QStringLiteral("Troppi poli selezionati: ridurre il numero di facce.");
            details_ += QStringLiteral("F%1: %2 × %3%4\n")
                            .arg(face.index)
                            .arg(surface->uPoleCount())
                            .arg(surface->vPoleCount())
                            .arg(p.refitU ? QStringLiteral("; scarto %1 mm").arg(error, 0, 'g', 6) : QString());
            records.append(entry);
            grids.append({surface, entry.edits, QStringLiteral("F%1").arg(face.index)});
        }
    } catch (const std::exception &error) {
        return QString::fromUtf8(error.what());
    }
    faces_ = faces;
    if (legacy_) {
        p.basis = records[0].basis;
        model.setSurface(grids[0].surface, grids[0].edits);
    } else {
        p.basis.clear();
        p.edits.clear();
        p.poleFaces = records;
        model.setFaces(grids);
    }
    return {};
}
void SurfacePolesSession::cacheRefit(const QVector<Kernel::FaceId> &faces,
                                     const std::vector<Kernel::BSplineSurfacePtr> &sources,
                                     const Kernel::SurfaceRefitGrid &grid) {
    if (faces.size() != int(sources.size()) || sources.size() != grid.faces.size())
        return;
    for (int i = 0; i < faces.size(); ++i)
        cache_[faces[i].index] = {sources[i], grid.faces[i].surface, grid.u, grid.v, grid.faces[i].sampledDeviation};
}
void SurfacePolesRefitSearch::cancel() {
    ++generation_;
    if (cancellation_)
        cancellation_->store(true, std::memory_order_relaxed);
    cancellation_.reset();
}
void SurfacePolesRefitSearch::start(std::vector<Kernel::BSplineSurfacePtr> sources, int u, int v, double tolerance) {
    cancel();
    cancellation_ = std::make_shared<std::atomic<bool>>(false);
    const auto token = cancellation_;
    const auto generation = generation_;
    const double accuracy = Kernel::currentCalculationAccuracy;
    const QPointer<SurfacePolesRefitSearch> guard(this);
    QThreadPool::globalInstance()->start(QRunnable::create([guard, token, generation, accuracy, sources = std::move(sources), u, v, tolerance] {
        Kernel::CancellationScope scope(token);
        Kernel::SurfaceRefitGrid grid;
        QString error;
        try {
            Kernel::PrecisionScope precision(accuracy);
            grid = Kernel::refitSurfacePolesWithinTolerance(sources, u, v, tolerance);
        } catch (const Kernel::OperationCancelled &) { return; }
        catch (const std::exception &e) { error = QString::fromUtf8(e.what()); }
        // La consegna passa dall'applicazione: guard e token vengono
        // controllati sul thread GUI prima di toccare il destinatario.
        if (token->load(std::memory_order_relaxed))
            return;
        QMetaObject::invokeMethod(QCoreApplication::instance(), [guard, token, generation, grid = std::move(grid), error] {
            if (guard && !token->load(std::memory_order_relaxed) && generation == guard->generation_ && guard->finished)
                guard->finished(grid, error);
        }, Qt::QueuedConnection);
    }));
}
} // namespace ForgeCad
