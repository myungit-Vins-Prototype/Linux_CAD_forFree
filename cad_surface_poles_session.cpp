#include "cad_surface_poles_session.h"
#include "cad_surface_tools.h"
#include "cad_topology_ref.h"
#include "fk_surface_refit.h"
#include <climits>
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
} // namespace ForgeCad
