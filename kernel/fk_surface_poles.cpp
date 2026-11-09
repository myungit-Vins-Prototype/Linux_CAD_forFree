#include "fk_body_check.h"
#include "fk_cancel.h"
#include "fk_offset.h"
#include "fk_pcurve.h"
#include "fk_surface_tools.h"
#include "fk_surface_poles_conversion.h"
#include <algorithm>
#include <set>
#include <stdexcept>
namespace ForgeCad::Kernel {
namespace {
void demand(bool condition, const char *message) {
    if (!condition)
        throw std::domain_error(message);
}
BSplineSurfacePtr changedSupport(const Body &sheet, const SurfaceFacePoleChange &edit, double tolerance) {
    demand(sheet.contains(edit.face), "poli: faccia non disponibile");
    const auto *s = dynamic_cast<const BSplineSurface *>(sheet.face(edit.face).surface.get());
    demand(s, "poli: il supporto selezionato non e' NURBS");
    const auto &replacement = edit.replacement;
    const auto &changes = edit.changes;
    if (replacement) {
        demand(replacement->uDomain().lo == s->uDomain().lo && replacement->uDomain().hi == s->uDomain().hi &&
                   replacement->vDomain().lo == s->vDomain().lo && replacement->vDomain().hi == s->vDomain().hi,
               "poli: la ricostruzione deve conservare il dominio UV");
        s = replacement.get();
    }
    demand((replacement || !changes.empty()) && changes.size() <= 4096, "poli: nessuna modifica o troppi poli");
    std::vector<Vec3> poles;
    std::vector<double> weights;
    for (int i = 0; i < s->uPoleCount(); ++i)
        for (int j = 0; j < s->vPoleCount(); ++j) {
            poles.push_back(s->pole(i, j));
            weights.push_back(s->weight(i, j));
        }
    std::set<std::pair<int, int>> seen;
    for (const auto &change : changes) {
        demand(change.u >= 0 && change.u < s->uPoleCount() && change.v >= 0 && change.v < s->vPoleCount() &&
                   seen.insert({change.u, change.v}).second,
               "poli: indice duplicato o fuori griglia");
        demand(isFinite(change.delta) && std::isfinite(change.weightFactor) && change.weightFactor > 0 &&
                   change.weightFactor <= 1e6,
               "poli: coordinate o peso non validi");
        const auto index = change.u * s->vPoleCount() + change.v;
        poles[index] += change.delta;
        weights[index] *= change.weightFactor;
        demand(std::isfinite(weights[index]) && weights[index] >= 1e-9 && weights[index] <= 1e9,
               "poli: peso risultante fuori intervallo");
    }
    auto support =
        std::make_shared<BSplineSurface>(s->uDegree(), s->vDegree(), s->uKnots(), s->vKnots(), s->uPoleCount(),
                                         s->vPoleCount(), std::move(poles), std::move(weights));
    const auto &original=static_cast<const BSplineSurface &>(*sheet.face(edit.face).surface);
    checkSurfacePoleClosure(original,*support,tolerance);
    const auto u = support->uDomain(), v = support->vDomain();
    for (int i = 0; i <= 24; ++i)
        for (int j = 0; j <= 24; ++j) {
            cancellationPoint();
            Vec3 d[4];
            support->evaluate(u.lo + u.length() * i / 24, v.lo + v.length() * j / 24, 1, d);
            Vec3 neutral[4];
            original.evaluate(u.lo+u.length()*i/24,v.lo+v.length()*j/24,1,neutral);
            const bool existingBoundaryPole=(i==0 || i==24 || j==0 || j==24) && norm(cross(neutral[1],neutral[2]))<=1e-12;
            demand(isFinite(d[0]) && isFinite(d[1]) && isFinite(d[2]) &&
                       (norm(cross(d[1], d[2])) > 1e-12 || existingBoundaryPole),
                   "poli: la modifica rende la superficie singolare");
        }
    return support;
}
} // namespace
Body editBodyFacesPoles(const Body &source, const std::vector<SurfaceFacePoleChange> &changes, double tolerance,
                        BodyHistory *history) {
    demand(std::isfinite(tolerance) && tolerance >= 1e-9 && tolerance <= 0.01, "poli: tolleranza non valida");
    demand(!changes.empty() && changes.size() <= 256, "poli: scegliere da 1 a 256 facce");
    Body result = source;
    std::set<int> faces;
    std::size_t count = 0;
    for (const auto &edit : changes) {
        demand(faces.insert(edit.face.index).second, "poli: faccia duplicata");
        count += edit.changes.size();
        demand(count <= 4096, "poli: modificare fino a 4096 poli per funzione");
        result.face(edit.face).surface = changedSupport(source, edit,tolerance);
    }
    // Le SP-curve appartengono alla topologia originale. Calcolarle prima di
    // sostituire i supporti, mai proiettare i vecchi bordi sui supporti deformati.
    Body uvSource = source;
    demand(computePCurves(uvSource) == 0, "poli: rifilature senza coordinate UV valide");
    for (const auto fin : result.fins()) {
        result.fin(fin).pcurve = uvSource.fin(fin).pcurve;
        result.fin(fin).pcurveTolerance = uvSource.fin(fin).pcurveTolerance;
    }
    // La tolleranza locale dei vertici importati e una connessione gia'
    // dichiarata dal B-rep, distinta dall'errore richiesto al fit dei bordi.
    std::set<int> touchedVertices;
    for (const auto edge : result.edges()) {
        auto &boundary = result.edge(edge);
        FinId chosen;
        for (const auto fin : {boundary.forward, boundary.backward})
            if (fin.valid() && faces.count(result.finFace(fin).index))
                chosen = fin;
        if (!chosen.valid())
            continue;
        cancellationPoint();
        const auto uv = result.fin(chosen).pcurve;
        demand(bool(uv), "poli: bordo senza rifilatura UV");
        const auto support = result.face(result.finFace(chosen)).surface;
        boundary.curve = fitCurve(
            [&](double t) {
                const auto q = uv->point(t);
                return support->point(q.x(), q.y());
            },
            boundary.range, uv->breakpoints(boundary.range), tolerance * .25);
        boundary.tolerance = std::max(boundary.tolerance, tolerance);
        // Conserva le connessioni: uno spostamento incompatibile con l'altra
        // faccia viene rifiutato, senza scollegare o allargare le tolleranze.
        for (const auto fin : {boundary.forward, boundary.backward}) {
            if (!fin.valid())
                continue;
            const auto &use = result.fin(fin);
            demand(bool(use.pcurve), "poli: bordo adiacente senza coordinate UV");
            const auto adjacent = result.face(result.finFace(fin)).surface;
            const double allowed =
                std::max({tolerance, source.edge(edge).tolerance, uvSource.fin(fin).pcurveTolerance});
            demand(pcurveDeviation(*adjacent, *boundary.curve, *use.pcurve, boundary.range, 64) <= allowed,
                   "poli: le modifiche separano un bordo condiviso; spostare insieme i poli corrispondenti delle facce "
                   "adiacenti");
        }
        for (const auto &entry : {std::make_pair(result.edgeStart(edge), boundary.curve->point(boundary.range.lo)),
                                  std::make_pair(result.edgeEnd(edge), boundary.curve->point(boundary.range.hi))}) {
            if (touchedVertices.insert(entry.first.index).second)
                result.vertex(entry.first).point = entry.second;
            else
                demand(distance(result.vertex(entry.first).point, entry.second) <=
                           std::max(tolerance,source.vertex(entry.first).tolerance),
                       "poli: le rifilature non si chiudono dopo la modifica");
        }
    }
    // Anche gli spigoli non modificati incidenti a un vertice devono chiudersi.
    for (const auto edge : result.edges()) {
        const auto &e = result.edge(edge);
        for (const auto &entry :
             {std::make_pair(result.edgeStart(edge), e.range.lo), std::make_pair(result.edgeEnd(edge), e.range.hi)})
            if (touchedVertices.count(entry.first.index))
                demand(distance(result.vertex(entry.first).point, e.curve->point(entry.second)) <=
                           std::max({tolerance,source.vertex(entry.first).tolerance,source.edge(edge).tolerance}),
                       "poli: spostamento incompatibile con un vertice condiviso");
    }
    const auto issues = checkBody(result, {true, true});
    if (!issues.empty())
        throw std::domain_error("poli: superficie non valida: " + issues.front().message);
    if (history)
        *history = sameIndexHistory(source, result);
    return result;
}
Body editSheetPoles(const Body &sheet, const std::vector<SurfacePoleChange> &changes, double tolerance,
                    BodyHistory *history, const BSplineSurfacePtr &replacement) {
    demand(sheet.isSheet() && sheet.faces().size() == 1, "poli: questa chiamata richiede una sola faccia");
    return editBodyFacesPoles(sheet, {{sheet.faces().front(), changes, replacement}}, tolerance, history);
}
} // namespace ForgeCad::Kernel
