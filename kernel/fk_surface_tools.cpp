#include "fk_surface_tools.h"
#include "fk_body_check.h"
#include "fk_boolean.h"
#include "fk_cancel.h"
#include "fk_classify.h"
#include "fk_curve_algo.h"
#include "fk_intersect.h"
#include "fk_offset.h"
#include "fk_pcurve.h"
#include "fk_sew.h"
#include "fk_sheet.h"
#include "fk_surface_algo.h"
#include <algorithm>
#include <set>
#include <stdexcept>
namespace ForgeCad::Kernel {
namespace {
void demand(bool b, const char *s) {
    if (!b)
        throw std::domain_error(s);
}
void valid(const Body &body) {
    const auto issues = checkBody(body, {true, true});
    if (!issues.empty())
        throw std::domain_error("superficie non valida: " + issues.front().message);
}
std::pair<double, double> crossing(const PathSegment &a, const PathSegment &b, double tol) {
    demand(a.curve && b.curve && a.range.isFinite() && b.range.isFinite() && a.range.length() > 0 && b.range.length() > 0,
           "rete: curve limitate non valide");
    std::vector<std::pair<double, double>> roots;
    for (const Vec3 &axis : {Vec3(0, 0, 1), Vec3(0, 1, 0), Vec3(1, 0, 0)}) {
        cancellationPoint();
        const auto frame = normalFrame(axis, {});
        const auto pa = planarImage(*a.curve, a.range, frame), pb = planarImage(*b.curve, b.range, frame);
        if (!pa.curve || !pb.curve)
            continue;
        const auto intersections = intersectCurves(*pa.curve, pa.range, *pb.curve, pb.range, tol, true);
        for (const auto &hit : intersections.points) {
            const double s = pa.toCurve ? pa.toCurve(hit.s) : hit.s, t = pb.toCurve ? pb.toCurve(hit.t) : hit.t;
            if (distance(a.curve->point(s), b.curve->point(t)) > tol)
                continue;
            const bool duplicate = std::any_of(roots.begin(), roots.end(), [&](const auto &r) {
                return std::fabs(r.first - s) <= 1e-9 * std::max(1.0, a.range.length()) &&
                       std::fabs(r.second - t) <= 1e-9 * std::max(1.0, b.range.length());
            });
            if (!duplicate)
                roots.push_back({s, t});
        }
    }
    demand(roots.size() == 1, "rete: ogni coppia deve avere un solo incrocio 3D, senza sovrapposizioni");
    return roots.front();
}
bool onBody(const Body &body, const Vec3 &p, double tol) {
    for (const auto face : body.faces()) {
        const auto q = projectPoint(*body.face(face).surface, p);
        if (q.distance <= tol && classifyPointOnFace(body, face, q.point, tol) != PointLocation::Outside)
            return true;
    }
    return false;
}
} // namespace
Body curveNetworkSheet(const std::vector<PathSegment> &rows, const std::vector<PathSegment> &columns, double tolerance,
                       FaceAliases *labels) {
    demand(std::isfinite(tolerance) && tolerance >= 1e-9 && tolerance <= 0.01, "rete: tolleranza non valida");
    demand(rows.size() >= 2 && columns.size() >= 2 && rows.size() <= 16 && columns.size() <= 16,
           "rete: servono da 2 a 16 curve per direzione");
    const int m = int(rows.size()), n = int(columns.size());
    std::vector<std::vector<double>> u(m, std::vector<double>(n)), v(n, std::vector<double>(m));
    for (int i = 0; i < m; ++i)
        for (int j = 0; j < n; ++j) {
            const auto hit = crossing(rows[i], columns[j], tolerance);
            u[i][j] = hit.first;
            v[j][i] = hit.second;
        }
    const auto monotone = [](const auto &values) {
        const bool up = values.back() > values.front();
        for (std::size_t k = 1; k < values.size(); ++k)
            if ((up ? values[k] - values[k - 1] : values[k - 1] - values[k]) <= 1e-10)
                return false;
        return true;
    };
    for (const auto &r : u)
        demand(monotone(r), "rete: riordinare le curve V; incroci coincidenti o non monotoni");
    for (const auto &c : v)
        demand(monotone(c), "rete: riordinare le curve U; incroci coincidenti o non monotoni");
    std::vector<Body> patches;
    patches.reserve((m - 1) * (n - 1));
    FaceAliases names;
    const auto part = [](const PathSegment &curve, double a, double b) {
        return PathSegment{curve.curve, {std::min(a, b), std::max(a, b)}};
    };
    for (int i = 0; i < m - 1; ++i)
        for (int j = 0; j < n - 1; ++j) {
            cancellationPoint();
            patches.push_back(boundarySheet({part(rows[i], u[i][j], u[i][j + 1]), part(columns[j + 1], v[j + 1][i], v[j + 1][i + 1]),
                                             part(rows[i + 1], u[i + 1][j], u[i + 1][j + 1]), part(columns[j], v[j][i], v[j][i + 1])},
                                            tolerance));
            names.push_back({"cell." + std::to_string(i) + "." + std::to_string(j)});
        }
    std::vector<const Body *> pointers;
    for (const auto &p : patches)
        pointers.push_back(&p);
    auto sewn = sewSheets(pointers, tolerance, false);
    demand(sewn.shells == 1 && sewn.body.faces().size() == patches.size(), "rete: celle non connesse o sovrapposte");
    valid(sewn.body);
    if (labels)
        *labels = std::move(names);
    return std::move(sewn.body);
}
Body extendSheetTo(const Body &sheet, EdgeId edge, const Body &tool, double maximum, bool linear, double tolerance, BodyHistory *history) {
    demand(sheet.isSheet() && sheet.contains(edge) && sheet.isLaminar(edge), "estensione: scegliere un bordo libero della superficie");
    demand(std::isfinite(maximum) && maximum > 0 && maximum <= 1e6, "estensione: distanza massima non valida");
    demand(std::isfinite(tolerance) && tolerance >= 1e-9 && tolerance <= 0.01, "estensione: tolleranza non valida");
    BooleanOptions options;
    options.tolerance = tolerance;
    options.threads = 1;
    demand(splitSheet(sheet, tool, options).size() <= 1, "estensione: il riferimento attraversa la superficie iniziale");
    Body original = sheet, cutter = tool;
    computePCurves(original);
    computePCurves(cutter);
    const auto expanded = extendSheet(original, {edge}, maximum, linear);
    auto pieces = splitSheet(expanded, tool, options);
    demand(pieces.size() > 1,
           "estensione: il riferimento non taglia il prolungamento entro la distanza massima");
    Vec3 keep;
    demand(!sheet.faces().empty() && faceInteriorPoint(sheet, sheet.faces().front(), keep),
           "estensione: punto interno della base non disponibile");
    auto result = std::move(pieces[detail::closestSheetPiece(pieces, keep)]);
    computePCurves(result);
    for (const auto e : sheet.edges())
        for (int k = 0; k <= 8; ++k) {
            cancellationPoint();
            const auto &boundary = sheet.edge(e);
            demand(onBody(result, boundary.curve->point(boundary.range.lo + boundary.range.length() * k / 8.0), 10 * tolerance),
                   "estensione: il rifilo elimina parte della superficie iniziale");
        }
    // Ogni frontiera nuova deve essere sul riferimento oppure sulle due pareti
    // laterali del prolungamento. Nessun bordo lontano a distanza massima.
    std::vector<EdgeId> cap;
    for (const auto e : expanded.edges())
        if (expanded.isLaminar(e) && !onBody(original, expanded.vertex(expanded.edgeStart(e)).point, 10 * tolerance) &&
            !onBody(original, expanded.vertex(expanded.edgeEnd(e)).point, 10 * tolerance))
            cap.push_back(e);
    demand(!cap.empty(), "estensione: frontiera massima non identificabile senza ambiguita");
    bool reached = false;
    for (const auto e : result.edges())
        if (result.isLaminar(e)) {
            const auto &boundary = result.edge(e);
            const Vec3 p = boundary.curve->point((boundary.range.lo + boundary.range.hi) / 2);
            if (onBody(cutter, p, 10 * tolerance))
                reached = true;
            else
                for (const auto far : cap) {
                    const auto &limit = expanded.edge(far);
                    demand(projectPoint(*limit.curve, p, limit.range).distance > 10 * tolerance,
                           "estensione: il riferimento non limita l intero bordo; aumentare la distanza o scegliere un altra faccia");
                }
        }
    demand(reached, "estensione: nessun bordo finale sul riferimento");
    valid(result);
    if (history) {
        *history = inferHistory({&sheet}, result);
        for (const auto f : result.faces())
            for (const auto old : sheet.faces())
                if (sameSurface(*result.face(f).surface, *sheet.face(old).surface, tolerance))
                    history->declareOrigins(EntityKind::Face, f.index, {{0, EntityKind::Face, old.index, HistoryRelation::Modified}});
    }
    return result;
}
Body editSheetPoles(const Body &sheet, const std::vector<SurfacePoleChange> &changes, double tolerance, BodyHistory *history) {
    demand(sheet.isSheet() && sheet.faces().size() == 1,
           "poli: usare una superficie a una sola faccia; estrarre prima la faccia con Copia facce");
    const auto face = sheet.faces().front();
    const auto *s = dynamic_cast<const BSplineSurface *>(sheet.face(face).surface.get());
    demand(s, "poli: il supporto selezionato non e' NURBS");
    demand(!changes.empty() && changes.size() <= 4096, "poli: nessuna modifica o troppi poli");
    demand(std::isfinite(tolerance) && tolerance >= 1e-9 && tolerance <= 0.01, "poli: tolleranza non valida");
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
        demand(isFinite(change.delta) && std::isfinite(change.weightFactor) && change.weightFactor > 0 && change.weightFactor <= 1e6,
               "poli: coordinate o peso non validi");
        const auto index = change.u * s->vPoleCount() + change.v;
        poles[index] += change.delta;
        weights[index] *= change.weightFactor;
        demand(std::isfinite(weights[index]) && weights[index] >= 1e-9 && weights[index] <= 1e9, "poli: peso risultante fuori intervallo");
    }
    auto support = std::make_shared<BSplineSurface>(s->uDegree(), s->vDegree(), s->uKnots(), s->vKnots(), s->uPoleCount(), s->vPoleCount(),
                                                    std::move(poles), std::move(weights));
    const auto u = support->uDomain(), v = support->vDomain();
    for (int i = 0; i <= 24; ++i)
        for (int j = 0; j <= 24; ++j) {
            cancellationPoint();
            Vec3 d[4];
            support->evaluate(u.lo + u.length() * i / 24, v.lo + v.length() * j / 24, 1, d);
            demand(isFinite(d[0]) && isFinite(d[1]) && isFinite(d[2]) && norm(cross(d[1], d[2])) > 1e-12,
                   "poli: la modifica rende la superficie singolare");
        }
    Body result = sheet;
    demand(computePCurves(result) == 0, "poli: rifilature senza coordinate UV valide");
    result.face(face).surface = support;
    std::vector<std::pair<VertexId, Vec3>> endpoints;
    for (const auto e : result.edges()) {
        cancellationPoint();
        auto &boundary = result.edge(e);
        const auto fin = boundary.forward.valid() ? boundary.forward : boundary.backward;
        const auto uv = result.fin(fin).pcurve;
        demand(bool(uv), "poli: bordo senza rifilatura UV");
        double deviation = 0;
        const auto curve = fitCurve(
            [&](double t) {
                const auto q = uv->point(t);
                return support->point(q.x(), q.y());
            },
            boundary.range, uv->breakpoints(boundary.range), tolerance, &deviation);
        boundary.curve = curve;
        boundary.tolerance = std::max(boundary.tolerance, tolerance);
        result.fin(fin).pcurveTolerance = tolerance;
        endpoints.push_back({result.edgeStart(e), curve->point(boundary.range.lo)});
        endpoints.push_back({result.edgeEnd(e), curve->point(boundary.range.hi)});
    }
    std::set<int> assigned;
    for (const auto &entry : endpoints) {
        if (assigned.insert(entry.first.index).second)
            result.vertex(entry.first).point = entry.second;
        else
            demand(distance(result.vertex(entry.first).point, entry.second) <= 10 * tolerance,
                   "poli: le rifilature non si chiudono dopo la modifica");
        result.vertex(entry.first).tolerance = std::max(result.vertex(entry.first).tolerance, 10 * tolerance);
    }
    valid(result);
    if (history)
        *history = sameIndexHistory(sheet, result);
    return result;
}
} // namespace ForgeCad::Kernel
