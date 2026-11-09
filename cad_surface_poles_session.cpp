#include "cad_surface_poles_session.h"
#include "cad_surface_tools.h"
#include "cad_topology_ref.h"
#include "fk_surface_refit.h"
#include "fk_surface_poles_conversion.h"
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
        return model.edits(p.edits,p.poleContinuity!=0);
    auto output = p.poleFaces;
    int count = 0;
    for (int i = 0; i < faces_.size(); ++i) {
        if (i >= output.size())
            return QStringLiteral("Selezione delle facce da aggiornare.");
        const auto error = model.faceEdits(i, output[i].edits,p.poleContinuity!=0);
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
    faces_.clear();constraints_.reset();continuity_={};drivers_.clear();hasDrivers_=false;
    model.setSurface({});
    deviation_ = 0;
    details_.clear();
    if (faces.isEmpty())
        return QStringLiteral("Scegli almeno una faccia NURBS.");
    if (faces.size() > 256)
        return QStringLiteral("Scegli fino a 256 facce NURBS.");
    legacy_ = body->faces().size() == 1 && p.poleFaces.isEmpty() &&
        body->face(faces.front()).surface->type()==Kernel::SurfaceType::BSpline;
    QVector<SurfacePoleFace> records;
    QVector<SurfacePolesModel::FaceGrid> grids;
    qint64 rows = 0;
    try {
        if(originalSource_!=body || preparedFaces_!=faces) {
            preparedSource_=std::make_shared<const Kernel::Body>(Kernel::prepareSurfacePoleBody(*body,{faces.cbegin(),faces.cend()},p.tolerance));
            originalSource_=body;preparedFaces_=faces;
        }
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
            auto surface = sourceSurface(face);
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
            if(body->face(face).surface->type()!=Kernel::SurfaceType::BSpline)
                details_+=QStringLiteral("  Superficie analitica convertita in NURBS esatta.\n");
            records.append(entry);
            grids.append({surface, entry.edits, QStringLiteral("F%1").arg(face.index)});
        }
    } catch (const std::exception &error) {
        return QString::fromUtf8(error.what());
    }
    if(p.poleContinuity)try {
        std::vector<Kernel::SurfaceFacePoleChange> bases;
        for(int i=0;i<faces.size();++i)bases.push_back({faces[i],{},p.refitU?grids[i].surface:Kernel::BSplineSurfacePtr{}});
        constraints_=std::make_unique<Kernel::SurfacePoleConstraints>(*preparedSource_,bases,p.poleContinuity,p.tolerance);
        deviation_=std::max(deviation_,constraints_->refitDeviation());
    }catch(const std::exception &error){return QString::fromUtf8(error.what());}
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
    model.setConstraintMode(p.poleContinuity!=0);
    return {};
}
QString SurfacePolesSession::constrain(SurfacePolesModel &model,SurfaceToolParameters &p) {
    const bool action=hasDrivers_;hasDrivers_=false;const QSet<int> activeRows(drivers_.cbegin(),drivers_.cend());
    if(!constraints_){QSignalBlocker guard(&model);model.setAutomatic({});return {};}
    auto error=store(model,p);if(!error.isEmpty())return error;
    std::vector<Kernel::SurfaceFacePoleChange> drivers;
    for(int i=0;i<faces_.size();++i){Kernel::SurfaceFacePoleChange face;face.face=faces_[i];
        const auto edits=legacy_?p.edits:p.poleFaces[i].edits;
        for(const auto &e:edits)face.changes.push_back({e.u,e.v,{e.dx,e.dy,e.dz},e.weightFactor});
        drivers.push_back(std::move(face));
    }
    try {
        std::vector<std::array<int,3>> active;
        if(action)for(int row:activeRows)if(row>=0&&row<model.rowCount()){
            const int f=model.faceIndex(row);const auto surface=model.surfaceForRow(row);const int local=model.faceRow(row);
            active.push_back({faces_[f<0?0:f].index,local/surface->vPoleCount(),local%surface->vPoleCount()});
        }
        const auto result=constraints_->propagate(drivers,&continuity_,action?&active:nullptr);
        QVector<QVector<SurfacePoleEdit>> automatic(faces_.size());
        for(const auto &face:result){const int i=faces_.indexOf(face.face);if(i<0)continue;
            for(const auto &e:face.changes)automatic[i].append({e.u,e.v,e.delta.x(),e.delta.y(),e.delta.z(),e.weightFactor});}
        {QSignalBlocker guard(&model);model.setAutomatic(automatic,activeRows,action);}
        continuity_.followers=model.automaticRows().size();
        return store(model,p);
    }catch(const std::exception &error){QSignalBlocker guard(&model);model.setAutomatic({});return QString::fromUtf8(error.what());}
    return {};
}
QString SurfacePolesSession::continuityText()const {
    if(!constraints_)return {};
    if(!continuity_.connected)return QStringLiteral("Nessun giunto condiviso: la faccia è isolata.");
    return QStringLiteral("%1 giunti; %2 tangenti, %3 spigoli; %4 poli spostati automaticamente (verdi). %5 giunti protetti con griglie non compatibili.")
        .arg(continuity_.connected).arg(continuity_.tangent).arg(continuity_.sharp).arg(continuity_.followers).arg(continuity_.protectedJoints);
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
