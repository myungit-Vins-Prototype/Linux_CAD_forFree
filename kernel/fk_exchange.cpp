#include "fk_exchange.h"

#include <charconv>

#include <algorithm>
#include <cmath>
#include <exception>
#include <map>
#include <set>
#include <stdexcept>

#include "fk_body_check.h"
#include "fk_curve_algo.h"
#include "fk_curve_ops.h"
#include "fk_parallel.h"
#include "fk_pcurve.h"
#include "fk_surface_algo.h"

namespace ForgeCad::Kernel::detail {

std::string formatReal(double x) {
    if (x == 0.0) return "0.";
    char buffer[40];
    const std::to_chars_result r = std::to_chars(buffer, buffer + sizeof buffer, x, std::chars_format::general, 17);
    std::string s(buffer, r.ptr);
    for (char &c : s)
        if (c == 'e') c = 'E';
    const std::size_t e = s.find('E');
    std::string mantissa = e == std::string::npos ? s : s.substr(0, e), exponent = e == std::string::npos ? "" : s.substr(e);
    if (mantissa.find('.') == std::string::npos) mantissa += '.';
    // Esponente senza '+' e senza zeri iniziali superflui ("E+05" -> "E5").
    if (!exponent.empty()) {
        std::string digits = exponent.substr(1);
        const bool negative = !digits.empty() && digits[0] == '-';
        if (!digits.empty() && (digits[0] == '+' || digits[0] == '-')) digits.erase(0, 1);
        while (digits.size() > 1 && digits[0] == '0') digits.erase(0, 1);
        exponent = std::string("E") + (negative ? "-" : "") + digits;
    }
    return mantissa + exponent;
}

std::size_t parseReal(const char *begin, const char *end, double &value) {
    const char *p = begin;
    if (p < end && *p == '+') ++p;
    // La grammatica di strtod nel locale "C" ("1.", ".5", "2.E-07").
    const std::from_chars_result r = std::from_chars(p, end, value, std::chars_format::general);
    if (r.ec != std::errc() || r.ptr == p) return 0;
    return std::size_t(r.ptr - begin);
}
namespace {

// La cucitura della faccia (edge usato due volte) si toglie se la superficie e'
// periodica nella direzione in cui l'edge la attraversa (u costante: periodica in u).
bool dropsSeam(const Surface &surface, const RawEdge &edge, const Interval &range) {
    if (!surface.isUPeriodic() && !surface.isVPeriodic()) return false;
    Vec2 uv[3];
    for (int k = 0; k < 3; ++k) {
        const SurfaceProjection p = projectPoint(surface, edge.curve->point(range.lo + range.length() * (0.2 + 0.3 * k)));
        uv[k] = Vec2(p.u, p.v);
    }
    const double du = std::fabs(uv[2].x() - uv[0].x()) + std::fabs(uv[1].x() - uv[0].x());
    const double dv = std::fabs(uv[2].y() - uv[0].y()) + std::fabs(uv[1].y() - uv[0].y());
    if (du <= dv) return surface.isUPeriodic();  // u costante lungo l'edge
    return surface.isVPeriodic();
}

}  // namespace

// Una sfera si puo' parametrizzare con qualsiasi asse per il centro. Nei file
// l'asse e' arbitrario: un edge che passa vicino a un polo senza toccarlo (un
// cerchio massimo inclinato di pochi millesimi su un meridiano, L407-P3.STEP)
// ha nello spazio (u, v) una curva che gira di quasi mezzo giro in un tratto
// minuscolo, e l'SP-curve non si approssima. Le sfere delle facce con fin
// senza SP-curve prendono l'asse con i poli piu' lontani da tutti gli edge
// delle loro facce (la geometria non cambia, solo i parametri); le SP-curve di
// quelle facce si rifanno. Vero se qualche sfera e' cambiata.
static bool reorientSpheres(Body &body) {
    std::map<const SphericalSurface *, std::vector<FaceId>> spheres;
    std::set<const SphericalSurface *> failing;
    for (FaceId f : body.faces()) {
        const auto *sphere = dynamic_cast<const SphericalSurface *>(body.face(f).surface.get());
        if (!sphere) continue;
        spheres[sphere].push_back(f);
        for (LoopId l : body.face(f).loops)
            for (FinId fin : body.loopFins(l))
                if (!body.fin(fin).pcurve) failing.insert(sphere);
    }
    bool changed = false;
    for (const SphericalSurface *sphere : failing) {
        const std::vector<FaceId> &faces = spheres[sphere];
        const Vec3 center = sphere->frame().origin();
        std::vector<Vec3> directions;  // dal centro ai punti degli edge
        std::vector<Vec3> candidates{sphere->frame().zDir(), sphere->frame().xDir(), sphere->frame().yDir(),
                                     Vec3(1, 0, 0), Vec3(0, 1, 0), Vec3(0, 0, 1)};
        for (FaceId f : faces)
            for (LoopId l : body.face(f).loops)
                for (FinId fin : body.loopFins(l)) {
                    const Edge &edge = body.edge(body.fin(fin).edge);
                    if (!edge.curve || !edge.range.isFinite()) continue;
                    if (const auto *circle = dynamic_cast<const Circle<3> *>(edge.curve.get()))
                        candidates.push_back(cross(circle->xAxis(), circle->yAxis()));
                    for (int k = 0; k <= 32; ++k) {
                        const Vec3 d = edge.curve->point(edge.range.lo + edge.range.length() * k / 32.0) - center;
                        if (norm(d) > 0.0) directions.push_back(d / norm(d));
                    }
                }
        // Direzioni sparse in modo uniforme (spirale di Fibonacci).
        for (int k = 0; k < 400; ++k) {
            const double z = 1.0 - (k + 0.5) / 200.0, r = std::sqrt(std::max(0.0, 1.0 - z * z)), a = 2.399963229728653 * k;
            candidates.push_back(Vec3(r * std::cos(a), r * std::sin(a), z));
        }
        // Punteggio: il seno dell'angolo piu' piccolo tra un punto e l'asse (i due poli).
        const auto score = [&](const Vec3 &axis) {
            double worst = 1.0;
            for (const Vec3 &d : directions) worst = std::min(worst, norm(cross(d, axis)));
            return worst;
        };
        const double before = score(sphere->frame().zDir());
        Vec3 best = sphere->frame().zDir();
        double bestScore = before;
        for (Vec3 axis : candidates) {
            if (!(norm(axis) > 0.0)) continue;
            axis = axis / norm(axis);
            const double value = score(axis);
            if (value > bestScore) bestScore = value, best = axis;
        }
        if (!(bestScore > before + 1e-3)) continue;
        // Asse X qualsiasi normale al nuovo asse; il sistema resta destrorso e
        // la normale uscente, quindi il verso delle facce non cambia.
        Vec3 x = std::fabs(best[0]) < 0.9 ? Vec3(1, 0, 0) : Vec3(0, 1, 0);
        x = x - dot(x, best) * best;
        const SurfacePtr replacement = std::make_shared<SphericalSurface>(Frame3(center, best, x / norm(x)), sphere->radius());
        for (FaceId f : faces) {
            body.face(f).surface = replacement;
            for (LoopId l : body.face(f).loops)
                for (FinId fin : body.loopFins(l)) {
                    body.fin(fin).pcurve = nullptr;
                    body.fin(fin).pcurveTolerance = 0.0;
                }
        }
        changed = true;
    }
    return changed;
}

// Verso delle facce su superfici non periodiche: la faccia sta a sinistra dei
// suoi loop rispetto alla sua normale, quindi nello spazio (u, v) l'area con
// segno dei loop e' positiva se la normale della faccia e' quella della
// superficie (sense) e negativa altrimenti. I loop vengono dalla topologia, che
// e' coerente con le facce vicine (ogni edge e' percorso nei due versi); il
// flag same_sense del file puo' essere sbagliato (L407-P3.STEP di SolidWorks:
// due raccordi B-spline con la normale opposta alle facce tangenti accanto).
// Si corregge il flag; le superfici periodiche (loop avvolti) non si toccano.
// Restituisce le facce corrette.
static int repairFaceSenses(Body &body) {
    int flipped = 0;
    for (FaceId f : body.faces()) {
        Face &face = body.face(f);
        if (!face.surface || face.surface->isUPeriodic() || face.surface->isVPeriodic()) continue;
        double area = 0.0, size = 0.0;
        bool complete = true;
        for (LoopId l : face.loops) {
            std::vector<Vec2> polygon;
            for (FinId finId : body.loopFins(l)) {
                const Fin &fin = body.fin(finId);
                const Edge &edge = body.edge(fin.edge);
                if (!fin.pcurve || !edge.range.isFinite()) {
                    complete = false;
                    break;
                }
                std::vector<double> breaks = edge.curve->breakpoints(edge.range);
                if (breaks.size() < 2 || breaks.size() > 64) breaks = {edge.range.lo, edge.range.hi};
                std::vector<double> ts;
                for (std::size_t b = 0; b + 1 < breaks.size(); ++b)
                    for (int k = 0; k < 16; ++k) ts.push_back(breaks[b] + (breaks[b + 1] - breaks[b]) * k / 16.0);
                if (!fin.sense) {
                    for (double &t : ts) t = edge.range.lo + edge.range.hi - t;
                }
                for (double t : ts) polygon.push_back(fin.pcurve->point(t));
            }
            if (!complete) break;
            for (std::size_t i = 0; i < polygon.size(); ++i) {
                const Vec2 &a = polygon[i], &b = polygon[(i + 1) % polygon.size()];
                area += 0.5 * (a[0] * b[1] - b[0] * a[1]);
                size += 0.5 * std::fabs(a[0] * b[1] - b[0] * a[1]);
            }
        }
        if (!complete || face.loops.empty()) continue;
        // Solo se il segno e' netto (loop degeneri o quasi a area nulla restano come sono).
        if (!(std::fabs(area) > 1e-6 * size)) continue;
        if ((area > 0.0) != face.sense) {
            face.sense = !face.sense;
            ++flipped;
        }
    }
    return flipped;
}

Body assembleBody(const RawModel &input, bool solid, std::vector<std::string> *notes) {
    RawModel model = input;
    const auto note = [&](const std::string &text) {
        if (notes) notes->push_back(text);
    };
    // Tratti degli edge dalle proiezioni dei vertici.
    std::vector<bool> flipped(model.edges.size(), false);
    for (std::size_t k = 0; k < model.edges.size(); ++k) {
        RawEdge &e = model.edges[k];
        if (!e.curve) throw std::domain_error("edge senza curva");
        if (e.start < 0 || e.end < 0 || e.start >= int(model.points.size()) || e.end >= int(model.points.size())) throw std::domain_error("edge senza vertici");
        if (e.hasRange) continue;
        const Curve<3> &curve = *e.curve;
        const Interval domain = curve.domain();
        double ts = projectPoint(curve, model.points[std::size_t(e.start)]).parameter;
        double te = projectPoint(curve, model.points[std::size_t(e.end)]).parameter;
        if (curve.isPeriodic()) {
            const double period = curve.period();
            if (e.start == e.end) {
                te = ts + period;
            } else {
                while (te <= ts + 1e-12 * period) te += period;
                while (te - ts > period) te -= period;
            }
        } else if (e.start == e.end) {
            if (!domain.isFinite()) throw std::domain_error("edge chiuso su una curva illimitata");
            ts = domain.lo, te = domain.hi;
        } else if (te < ts) {
            // La curva va dall'altra parte: si gira l'edge (e le sue fin).
            std::swap(e.start, e.end);
            std::swap(ts, te);
            flipped[k] = true;
        }
        if (!(te > ts)) throw std::domain_error("edge di lunghezza nulla");
        e.range = {ts, te};
        e.hasRange = true;
    }
    for (RawFace &face : model.faces)
        for (auto &loop : face.loops)
            for (RawFin &fin : loop)
                if (flipped[std::size_t(fin.edge)]) fin.sense = !fin.sense;

    // Cuciture e loop.
    std::vector<Body::BuildFace> faces;
    std::vector<int> pointIndex(model.points.size(), -1), edgeIndex(model.edges.size(), -1);
    std::vector<Vec3> points;
    std::vector<Body::BuildEdge> edges;
    struct Seam {
        std::size_t face;
        int edge;
    };
    std::vector<Seam> seams;
    const auto pointOf = [&](int p) {
        if (pointIndex[std::size_t(p)] < 0) {
            pointIndex[std::size_t(p)] = int(points.size());
            points.push_back(model.points[std::size_t(p)]);
        }
        return pointIndex[std::size_t(p)];
    };
    const auto edgeOf = [&](int e) {
        if (edgeIndex[std::size_t(e)] < 0) {
            const RawEdge &raw = model.edges[std::size_t(e)];
            Body::BuildEdge edge;
            edge.start = pointOf(raw.start);
            edge.end = pointOf(raw.end);
            edge.curve = raw.curve;
            edge.range = raw.range;
            edgeIndex[std::size_t(e)] = int(edges.size());
            edges.push_back(edge);
        }
        return edgeIndex[std::size_t(e)];
    };
    int droppedSeams = 0;
    for (const RawFace &raw : model.faces) {
        if (!raw.surface) throw std::domain_error("faccia senza superficie");
        std::map<int, int> uses;
        for (const auto &loop : raw.loops)
            for (const RawFin &fin : loop) ++uses[fin.edge];
        Body::BuildFace face;
        face.surface = raw.surface;
        face.sense = raw.sense;
        for (const auto &loop : raw.loops) {
            std::vector<RawFin> kept;
            for (const RawFin &fin : loop) {
                if (uses[fin.edge] == 2 && dropsSeam(*raw.surface, model.edges[std::size_t(fin.edge)], model.edges[std::size_t(fin.edge)].range)) {
                    ++droppedSeams;
                    continue;
                }
                kept.push_back(fin);
            }
            if (kept.empty()) continue;
            const auto startOf = [&](const RawFin &f) { return f.sense ? model.edges[std::size_t(f.edge)].start : model.edges[std::size_t(f.edge)].end; };
            const auto endOf = [&](const RawFin &f) { return f.sense ? model.edges[std::size_t(f.edge)].end : model.edges[std::size_t(f.edge)].start; };
            const std::size_t n = kept.size();
            std::size_t begin = 0;
            for (std::size_t i = 0; i < n; ++i)
                if (endOf(kept[(i + n - 1) % n]) != startOf(kept[i])) {
                    begin = i;
                    break;
                }
            std::vector<Body::BuildFin> current;
            int currentStart = -1, currentEnd = -1;
            const auto close = [&] {
                if (currentEnd != currentStart) throw std::domain_error("loop aperto");
                face.loops.push_back(current);
                current.clear();
            };
            for (std::size_t k = 0; k < n; ++k) {
                const RawFin &fin = kept[(begin + k) % n];
                if (!current.empty() && currentEnd != startOf(fin)) close();
                if (current.empty()) currentStart = startOf(fin);
                current.push_back({edgeOf(fin.edge), fin.sense, nullptr, 0.0});
                currentEnd = endOf(fin);
            }
            close();
        }
        for (const auto &[edge, count] : uses)
            if (count == 2 && edgeIndex[std::size_t(edge)] >= 0) {
                bool inFace = false;
                for (const auto &loop : face.loops)
                    for (const Body::BuildFin &fin : loop) inFace = inFace || fin.edge == edgeIndex[std::size_t(edge)];
                if (inFace) seams.push_back({faces.size(), edgeIndex[std::size_t(edge)]});
            }
        faces.push_back(std::move(face));
    }
    if (droppedSeams > 0) note(std::to_string(droppedSeams / 2) + " cuciture di superfici periodiche tolte");

    // SP-curve delle cuciture che restano: i due bordi del dominio, dal verso delle fin.
    for (const Seam &seam : seams) {
        Body::BuildFace &face = faces[seam.face];
        const Surface &surface = *face.surface;
        const Body::BuildEdge &edge = edges[std::size_t(seam.edge)];
        double deviation = 0.0;
        const CurvePtr<2> base = fitPCurve(surface, edge.curve, edge.range, std::max(1e-7, edge.tolerance), &deviation);
        if (!base) throw std::domain_error("SP-curve di una cucitura non calcolabile");
        const Interval ud = surface.uDomain(), vd = surface.vDomain();
        const Vec2 a = base->point(edge.range.lo), b = base->point(edge.range.hi), m = base->point(0.5 * (edge.range.lo + edge.range.hi));
        const bool alongV = std::fabs(b.x() - a.x()) < std::fabs(b.y() - a.y());  // u costante: la cucitura divide in u
        CurvePtr<2> low, high;
        if (alongV) {
            const double L = ud.length();
            const bool atLow = std::fabs(m.x() - ud.lo) < std::fabs(m.x() - ud.hi);
            low = atLow ? base : translatedCurve(base, Vec2(-L, 0.0));
            high = atLow ? translatedCurve(base, Vec2(L, 0.0)) : base;
        } else {
            const double L = vd.length();
            const bool atLow = std::fabs(m.y() - vd.lo) < std::fabs(m.y() - vd.hi);
            low = atLow ? base : translatedCurve(base, Vec2(0.0, -L));
            high = atLow ? translatedCurve(base, Vec2(0.0, L)) : base;
        }
        const double speed = alongV ? b.y() - a.y() : b.x() - a.x();
        for (auto &loop : face.loops)
            for (Body::BuildFin &fin : loop) {
                if (fin.edge != seam.edge) continue;
                const double direction = fin.sense ? speed : -speed;
                // Loop antiorario (faccia nel verso della superficie): sul bordo sinistro (u basso) si scende in v, sul
                // destro si sale; sul bordo in basso (v basso) si va verso u crescenti, in alto al contrario.
                const bool lowSide = alongV ? ((direction < 0.0) == face.sense) : ((direction > 0.0) == face.sense);
                fin.pcurve = lowSide ? low : high;
                fin.pcurveTolerance = std::max(deviation, std::numeric_limits<double>::min());
            }
    }

    Body body;
    try {
        body = solid ? Body::build(points, edges, faces) : Body::buildSheet(points, edges, faces);
    } catch (const std::invalid_argument &failure) {
        throw std::domain_error(failure.what());
    }
    // Tolleranze: vertici dagli estremi delle curve, edge dalle facce.
    // Gli scarti degli edge dalle facce (proiezioni sulle superfici: la parte
    // lunga dei file grandi) si misurano in parallelo, poi si applicano in ordine.
    std::vector<EdgeId> edgeIds;
    for (EdgeId e : body.edges()) edgeIds.push_back(e);
    std::vector<double> deviations(edgeIds.size(), 0.0);
    std::vector<std::exception_ptr> failures(edgeIds.size());
    const Body &measured = body;
    const auto measure = [&](std::size_t i) {
        const Edge &edge = measured.edge(edgeIds[i]);
        double deviation = 0.0;
        for (FinId fin : {edge.forward, edge.backward}) {
            if (!fin.valid()) continue;
            const Surface &surface = *measured.face(measured.finFace(fin)).surface;
            // Campioni fitti: lo scarto di una B-spline da una superficie libera cambia tra un nodo e l'altro.
            std::vector<double> breaks = edge.curve->breakpoints(edge.range);
            if (breaks.size() < 2 || breaks.size() > 64) breaks = {edge.range.lo, edge.range.hi};
            for (std::size_t b = 0; b + 1 < breaks.size(); ++b)
                for (int k = 0; k <= 16; ++k)
                    deviation = std::max(deviation, projectPoint(surface, edge.curve->point(breaks[b] + (breaks[b + 1] - breaks[b]) * k / 16.0)).distance);
        }
        deviations[i] = deviation;
    };
    parallelFor(edgeIds.size(), edgeIds.size() >= 256 ? threadCount(0) : 1u, [&](std::size_t i) {
        try {
            measure(i);
        } catch (...) {
            failures[i] = std::current_exception();  // si rilancia dopo, come in sequenza
        }
    });
    for (const std::exception_ptr &failure : failures)
        if (failure) std::rethrow_exception(failure);
    double worst = 0.0;
    for (std::size_t i = 0; i < edgeIds.size(); ++i) {
        const EdgeId e = edgeIds[i];
        Edge &edge = body.edge(e);
        for (const auto &[v, t] : {std::pair<VertexId, double>{body.edgeStart(e), edge.range.lo}, {body.edgeEnd(e), edge.range.hi}}) {
            const double d = distance(body.vertex(v).point, edge.curve->point(t));
            if (d > 1e-7) body.vertex(v).tolerance = std::max(body.vertex(v).tolerance, 1.01 * d);
        }
        const double deviation = deviations[i];
        if (deviation > 1e-7) {
            edge.tolerance = std::max(edge.tolerance, 1.5 * deviation);
            worst = std::max(worst, deviation);
        }
    }
    for (EdgeId e : body.edges())
        for (VertexId v : {body.edgeStart(e), body.edgeEnd(e)})
            if (body.edge(e).tolerance > 0.0) body.vertex(v).tolerance = std::max(body.vertex(v).tolerance, body.edge(e).tolerance);
    if (worst > 1e-6) note("edge tolleranti fino a " + std::to_string(worst) + " (precisione del file)");
    int missing = computePCurves(body);
    if (missing > 0 && reorientSpheres(body)) missing = computePCurves(body);
    if (missing > 0) throw std::domain_error(std::to_string(missing) + " SP-curve non calcolabili");
    if (const int flipped = repairFaceSenses(body))
        note(std::to_string(flipped) + " facce con il verso (same_sense) opposto ai loop: corretto");
    const std::vector<CheckIssue> issues = checkBody(body);
    if (!issues.empty()) throw std::domain_error("corpo non valido: " + describe(issues.front().code) + " (" + issues.front().message + ")");
    return body;
}

bool sewModel(RawModel &model, double tolerance) {
    // Vertici uniti: ordinati in x, confronti solo vicini.
    const std::size_t n = model.points.size();
    std::vector<int> order(n), target(n);
    for (std::size_t k = 0; k < n; ++k) order[k] = int(k), target[k] = int(k);
    std::sort(order.begin(), order.end(), [&](int a, int b) { return model.points[std::size_t(a)].x() < model.points[std::size_t(b)].x(); });
    for (std::size_t i = 0; i < n; ++i) {
        const int a = order[i];
        if (target[std::size_t(a)] != a) continue;
        for (std::size_t j = i + 1; j < n; ++j) {
            const int b = order[j];
            if (model.points[std::size_t(b)].x() - model.points[std::size_t(a)].x() > tolerance) break;
            if (target[std::size_t(b)] == b && distance(model.points[std::size_t(a)], model.points[std::size_t(b)]) <= tolerance) target[std::size_t(b)] = a;
        }
    }
    for (RawEdge &e : model.edges) {
        e.start = target[std::size_t(e.start)];
        e.end = target[std::size_t(e.end)];
    }
    // Edge uguali: stessi estremi e punto medio sull'altro.
    std::map<std::pair<int, int>, std::vector<int>> byEnds;
    for (std::size_t k = 0; k < model.edges.size(); ++k) {
        const RawEdge &e = model.edges[k];
        byEnds[{std::min(e.start, e.end), std::max(e.start, e.end)}].push_back(int(k));
    }
    std::vector<int> edgeTarget(model.edges.size());
    std::vector<bool> reversed(model.edges.size(), false);
    for (std::size_t k = 0; k < edgeTarget.size(); ++k) edgeTarget[k] = int(k);
    // Punto della curva alla frazione f del tratto (nel verso della curva).
    const auto at = [&](const RawEdge &e, double f) {
        if (e.hasRange) return e.curve->point(e.range.lo + f * e.range.length());
        // Senza tratto: tra le proiezioni degli estremi.
        double a = projectPoint(*e.curve, model.points[std::size_t(e.start)]).parameter, b = projectPoint(*e.curve, model.points[std::size_t(e.end)]).parameter;
        if (e.curve->isPeriodic() && b <= a) b += e.curve->period();
        return e.curve->point(a + f * (b - a));
    };
    const auto middle = [&](const RawEdge &e) { return at(e, 0.5); };
    for (const auto &[ends, list] : byEnds)
        for (std::size_t i = 0; i < list.size(); ++i) {
            if (edgeTarget[std::size_t(list[i])] != list[i]) continue;
            const RawEdge &a = model.edges[std::size_t(list[i])];
            const Vec3 ma = middle(a);
            for (std::size_t j = i + 1; j < list.size(); ++j) {
                if (edgeTarget[std::size_t(list[j])] != list[j]) continue;
                const RawEdge &b = model.edges[std::size_t(list[j])];
                if (distance(ma, middle(b)) > 10.0 * tolerance) continue;
                edgeTarget[std::size_t(list[j])] = list[i];
                // Il verso: dagli estremi, o per un edge chiuso da un punto a un quarto del tratto.
                if (a.start != a.end) reversed[std::size_t(list[j])] = a.start != b.start;
                else reversed[std::size_t(list[j])] = distance(at(a, 0.25), at(b, 0.25)) > distance(at(a, 0.25), at(b, 0.75));
            }
        }
    for (RawFace &face : model.faces)
        for (auto &loop : face.loops)
            for (RawFin &fin : loop) {
                if (reversed[std::size_t(fin.edge)]) fin.sense = !fin.sense;
                fin.edge = edgeTarget[std::size_t(fin.edge)];
            }
    // Versi coerenti: le due facce di un edge lo percorrono in versi opposti.
    std::map<int, std::vector<std::pair<std::size_t, bool>>> finsOf;  // edge -> (faccia, verso)
    for (std::size_t f = 0; f < model.faces.size(); ++f)
        for (const auto &loop : model.faces[f].loops)
            for (const RawFin &fin : loop) finsOf[fin.edge].push_back({f, fin.sense});
    std::vector<int> state(model.faces.size(), 0);  // 0 da vedere, 1 com'e', -1 da girare
    for (std::size_t seed = 0; seed < model.faces.size(); ++seed) {
        if (state[seed]) continue;
        state[seed] = 1;
        std::vector<std::size_t> queue{seed};
        while (!queue.empty()) {
            const std::size_t f = queue.back();
            queue.pop_back();
            for (const auto &loop : model.faces[f].loops)
                for (const RawFin &fin : loop) {
                    const auto &list = finsOf[fin.edge];
                    if (list.size() != 2) continue;
                    const auto &other = list[0].first == f && list[0].second == fin.sense ? list[1] : list[0];
                    if (other.first == f) continue;
                    // Stesso verso effettivo: l'altra faccia va girata rispetto a questa.
                    const bool mine = (state[f] > 0) == fin.sense, theirs = other.second;
                    const int wanted = mine == theirs ? -1 : 1;
                    if (!state[other.first]) {
                        state[other.first] = wanted;
                        queue.push_back(other.first);
                    }
                }
        }
    }
    for (std::size_t f = 0; f < model.faces.size(); ++f) {
        if (state[f] >= 0) continue;
        RawFace &face = model.faces[f];
        face.sense = !face.sense;
        for (auto &loop : face.loops) {
            std::reverse(loop.begin(), loop.end());
            for (RawFin &fin : loop) fin.sense = !fin.sense;
        }
    }
    bool closed = true;
    for (const auto &[edge, list] : finsOf) closed = closed && list.size() == 2;
    return closed;
}

}
